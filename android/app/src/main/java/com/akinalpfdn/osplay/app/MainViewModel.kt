package com.akinalpfdn.osplay.app

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.akinalpfdn.osplay.core.CoreError
import com.akinalpfdn.osplay.core.Identity
import com.akinalpfdn.osplay.core.Protocol
import com.akinalpfdn.osplay.core.SessionState
import com.akinalpfdn.osplay.discovery.DiscoveredDevice
import com.akinalpfdn.osplay.discovery.DiscoveryEvent
import com.akinalpfdn.osplay.storage.KeyStoreError
import com.akinalpfdn.osplay.storage.KeyStoreException
import com.akinalpfdn.osplay.storage.TrustCache
import com.akinalpfdn.osplay.storage.TrustedPeer
import com.akinalpfdn.osplay.viewer.MonitorDescription
import com.akinalpfdn.osplay.viewer.StatsFigures
import com.akinalpfdn.osplay.viewer.ViewerEvent
import com.akinalpfdn.osplay.viewer.ViewerSessionHandle
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** What a computer in the list can do with this device. */
enum class DeviceState { Paired, New, NeedsUpdate, Incompatible }

data class DeviceRow(val device: DiscoveredDevice, val name: String, val state: DeviceState)

/** The screen the app shows. */
sealed interface Screen {
    data object Home : Screen

    data class Connecting(val peer: String) : Screen

    data class Viewing(val peer: String) : Screen

    /** The connection dropped (sleep, network, rotation); waiting for the computer again. */
    data class Reconnecting(val peer: String) : Screen
}

/** Why the app cannot list, connect to or keep showing a computer. */
sealed interface Failure {
    data class KeyStore(val error: KeyStoreError) : Failure

    data class Discovery(val code: Int) : Failure

    data class Session(val error: CoreError) : Failure

    /** A different major protocol version. */
    data object Incompatible : Failure

    /** Osplay 0.1 on the computer refuses tablets and phones. */
    data class NeedsUpdate(val name: String) : Failure

    data object NoScreen : Failure
}

data class PairingRequest(val code: Int, val peerName: String)

/** The whole UI state (Kotlin rules: one state object). */
data class UiState(
    val screen: Screen = Screen.Home,
    val devices: List<DeviceRow> = emptyList(),
    val failure: Failure? = null,
    val pairing: PairingRequest? = null,
    val peers: List<TrustedPeer> = emptyList(),
    val deviceName: String = "",
    val showsStats: Boolean = false,
    val stats: StatsFigures? = null,
) {
    val showsViewer: Boolean get() = screen is Screen.Connecting || screen is Screen.Viewing
}

/**
 * Nearby computers, the pairing prompt and the one viewer session. The device is a display only
 * (DEVPLAN Phase 15): it never hosts and forwards no input.
 */
class MainViewModel(private val dependencies: AppDependencies) : ViewModel() {
    private val trust = TrustCache(dependencies.keyStore)
    private val state = MutableStateFlow(
        UiState(deviceName = dependencies.settings.deviceName, showsStats = dependencies.settings.showsStatsOverlay),
    )
    val uiState: StateFlow<UiState> = state.asStateFlow()

    private var identity: Identity? = null
    private var devices: List<DiscoveredDevice> = emptyList()
    private var browseJob: Job? = null
    private var reconnectJob: Job? = null
    private var session: ViewerSessionHandle? = null
    private var viewedHost: Pair<String, String>? = null
    private var offeredMonitor: MonitorDescription? = null
    private var endedByUser = false
    private var resumesAfterClose = false
    private var awaitedDeviceId: String? = null
    private var inBackground = false
    private var resumeDeviceId: String? = null

    // Nearby computers

    /** Loads the identity and paired computers and starts looking for computers. */
    fun start() {
        if (prepare() == null || browseJob != null) return
        browseJob = viewModelScope.launch {
            dependencies.discovery.browse().collect(::discovered)
            browseJob = null
        }
    }

    fun stateOf(device: DiscoveredDevice): DeviceState {
        val advertisement = device.advertisement
        return when {
            advertisement.protocolVersion != Protocol.VERSION -> DeviceState.Incompatible
            advertisement.protocolMinor == 0 -> DeviceState.NeedsUpdate
            trust.isPaired(advertisement.deviceId) -> DeviceState.Paired
            else -> DeviceState.New
        }
    }

    private fun nameOf(device: DiscoveredDevice): String =
        trust.nameOf(device.advertisement.deviceId) ?: device.advertisement.name

    private fun discovered(event: DiscoveryEvent) {
        when (event) {
            is DiscoveryEvent.Found -> {
                // Tablets and computers that only view cannot be extended onto.
                if (!event.device.advertisement.isHost) return
                devices = (devices.filterNot { it.label == event.device.label } + event.device)
                if (event.device.advertisement.deviceId == awaitedDeviceId) {
                    awaitedDeviceId = null
                    connect(event.device)
                }
            }
            is DiscoveryEvent.Lost -> devices = devices.filterNot { it.label == event.label }
            is DiscoveryEvent.Failed -> {
                devices = emptyList()
                state.update { it.copy(failure = Failure.Discovery(event.code)) }
            }
        }
        publishDevices()
    }

    private fun publishDevices() {
        val rows = devices.map { DeviceRow(it, nameOf(it), stateOf(it)) }.sortedBy { it.name.lowercase() }
        state.update { it.copy(devices = rows, peers = trust.peers) }
    }

    // Viewing

    fun connect(device: DiscoveredDevice) {
        if (session != null) return
        val identity = prepare() ?: return
        when (stateOf(device)) {
            DeviceState.Incompatible -> return fail(Failure.Incompatible)
            DeviceState.NeedsUpdate -> return fail(Failure.NeedsUpdate(nameOf(device)))
            DeviceState.Paired, DeviceState.New -> Unit
        }
        val monitor = dependencies.screen.monitor() ?: return fail(Failure.NoScreen)
        reconnectJob?.cancel()
        awaitedDeviceId = null
        val name = nameOf(device)
        val (started, error) = dependencies.sessions.start(
            identity = identity,
            deviceName = protocolDeviceName(),
            monitor = monitor,
            host = device.host,
            port = device.port,
            trust = trust::trust,
            onEvent = ::handle,
        )
        if (started == null) return fail(Failure.Session(error ?: CoreError.Internal))
        session = started
        viewedHost = device.advertisement.deviceId to name
        offeredMonitor = monitor
        endedByUser = false
        state.update { it.copy(screen = Screen.Connecting(name), failure = null, stats = null) }
    }

    /** Ends the session; nothing reconnects. */
    fun disconnect() {
        endedByUser = true
        awaitedDeviceId = null
        resumeDeviceId = null
        reconnectJob?.cancel()
        if (state.value.screen is Screen.Reconnecting) state.update { it.copy(screen = Screen.Home) }
        session?.stop()
    }

    fun answerPairing(accepted: Boolean) {
        state.update { it.copy(pairing = null) }
        session?.confirmPairing(accepted)
    }

    fun dismissFailure() = state.update { it.copy(failure = null) }

    /** The device turned: the host's display must take the new shape, so the session starts again. */
    fun viewportChanged(landscape: Boolean) {
        val offered = offeredMonitor ?: return
        if (session == null || endedByUser || offered.isLandscape == landscape) return
        resumesAfterClose = true
        session?.stop()
    }

    /** Android stops a background app's work, so the session ends cleanly and resumes on return. */
    fun onBackground() {
        inBackground = true
        if (session == null || endedByUser) return
        resumesAfterClose = true
        session?.stopForSleep()
    }

    fun onForeground() {
        inBackground = false
        start()
        resumeDeviceId?.let {
            resumeDeviceId = null
            awaitHost(it, 0)
        }
    }

    private fun handle(event: ViewerEvent) {
        when (event) {
            is ViewerEvent.Peer -> viewedHost?.let { (id, name) ->
                if (!trust.isPaired(id)) viewedHost = id to event.name.ifEmpty { name }
            }
            is ViewerEvent.PairingCode ->
                state.update { it.copy(pairing = PairingRequest(event.code, viewedHost?.second.orEmpty())) }
            is ViewerEvent.Paired -> store { trust.paired(event.publicKey, viewedHost?.second.orEmpty()) }
            is ViewerEvent.Seen -> store { trust.seen(event.publicKey) }
            is ViewerEvent.Statistics -> state.update { it.copy(stats = event.figures) }
            is ViewerEvent.State -> when (event.state) {
                SessionState.Streaming -> state.update { it.copy(screen = Screen.Viewing(viewedHost?.second.orEmpty())) }
                SessionState.Closed -> ended(event.failure)
                else -> Unit
            }
        }
    }

    private fun ended(failure: CoreError?) {
        session = null
        offeredMonitor = null
        val host = viewedHost
        val resumes = resumesAfterClose
        resumesAfterClose = false
        state.update { it.copy(pairing = null, stats = null) }
        val expectedBack = resumes || failure == CoreError.PeerSleeping || failure == CoreError.TimedOut ||
            failure == CoreError.Network
        if (host != null && expectedBack && !endedByUser) {
            state.update { it.copy(screen = Screen.Reconnecting(host.second)) }
            if (inBackground) resumeDeviceId = host.first else awaitHost(host.first, if (resumes) 0 else RECONNECT_DELAY_MS)
            return
        }
        viewedHost = null
        state.update {
            it.copy(
                screen = Screen.Home,
                failure = failure?.takeIf { error -> error != CoreError.ConnectionClosed }?.let(Failure::Session),
            )
        }
    }

    /** Connects when the computer is listed again (at once if it still is). */
    private fun awaitHost(deviceId: String, delayMillis: Long) {
        reconnectJob?.cancel()
        reconnectJob = viewModelScope.launch {
            delay(delayMillis)
            if (session != null) return@launch
            val device = devices.firstOrNull { it.advertisement.deviceId == deviceId }
            if (device != null) connect(device) else awaitedDeviceId = deviceId
        }
    }

    // Settings and paired computers

    fun setDeviceName(name: String) {
        dependencies.settings.deviceName = name
        state.update { it.copy(deviceName = name) }
    }

    fun setShowsStats(shows: Boolean) {
        dependencies.settings.showsStatsOverlay = shows
        state.update { it.copy(showsStats = shows) }
    }

    fun revoke(peer: TrustedPeer) = store { trust.revoke(peer) }

    fun remove(peer: TrustedPeer) = store { trust.remove(peer) }

    private fun protocolDeviceName(): String =
        com.akinalpfdn.osplay.screen.ProtocolText.name(dependencies.settings.deviceName.trim()).ifEmpty { DEFAULT_NAME }

    private fun prepare(): Identity? {
        identity?.let { return it }
        return try {
            val loaded = dependencies.keyStore.identity()
            trust.load()
            identity = loaded
            state.update { it.copy(peers = trust.peers) }
            loaded
        } catch (error: KeyStoreException) {
            fail(Failure.KeyStore(error.error))
            null
        }
    }

    private fun store(change: () -> Unit) {
        try {
            change()
        } catch (error: KeyStoreException) {
            fail(Failure.KeyStore(error.error))
        }
        publishDevices()
    }

    private fun fail(failure: Failure) = state.update { it.copy(failure = failure) }

    private companion object {
        const val RECONNECT_DELAY_MS = 2_000L
        const val DEFAULT_NAME = "Android"
    }
}
