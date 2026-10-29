package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.app.AppDependencies
import com.akinalpfdn.osplay.app.DeviceState
import com.akinalpfdn.osplay.app.MainViewModel
import com.akinalpfdn.osplay.app.SettingsStoring
import com.akinalpfdn.osplay.core.Advertisement
import com.akinalpfdn.osplay.core.CoreError
import com.akinalpfdn.osplay.core.Identity
import com.akinalpfdn.osplay.core.Platform
import com.akinalpfdn.osplay.core.Trust
import com.akinalpfdn.osplay.discovery.DiscoveredDevice
import com.akinalpfdn.osplay.discovery.DiscoveryEvent
import com.akinalpfdn.osplay.discovery.PeerDiscovering
import com.akinalpfdn.osplay.screen.ScreenDescribing
import com.akinalpfdn.osplay.storage.KeyStoring
import com.akinalpfdn.osplay.storage.PeerBook
import com.akinalpfdn.osplay.usb.CableEvent
import com.akinalpfdn.osplay.usb.CableOpening
import com.akinalpfdn.osplay.usb.UsbCabling
import com.akinalpfdn.osplay.viewer.MonitorDescription
import com.akinalpfdn.osplay.viewer.SessionTarget
import com.akinalpfdn.osplay.viewer.StreamStage
import com.akinalpfdn.osplay.viewer.ViewerEvent
import com.akinalpfdn.osplay.viewer.ViewerSessionHandle
import com.akinalpfdn.osplay.viewer.ViewerSessionMaking
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.emptyFlow
import org.junit.Assert.assertEquals
import org.junit.Test

class DeviceStateTest {
    private fun device(minor: Int, version: Int = 1) = DiscoveredDevice(
        label = "pc",
        advertisement = Advertisement(version, minor, Platform.Windows, roles = 1, deviceId = "ab".repeat(16), name = "PC"),
        host = "192.168.0.2",
        port = 4000,
    )

    private val model = MainViewModel(
        AppDependencies(
            keyStore = object : KeyStoring {
                override fun identity(): Identity = error("not needed")

                override fun peers() = PeerBook()

                override fun save(peers: PeerBook) = Unit
            },
            discovery = object : PeerDiscovering {
                override fun browse(): Flow<DiscoveryEvent> = emptyFlow()
            },
            screen = object : ScreenDescribing {
                override fun monitor(): MonitorDescription? = null
            },
            settings = object : SettingsStoring {
                override var deviceName = "Tablet"
                override var showsStatsOverlay = false
            },
            stage = StreamStage { _, _, _ -> error("no decoder in this test") },
            sessions = object : ViewerSessionMaking {
                override fun start(
                    identity: Identity,
                    deviceName: String,
                    monitor: MonitorDescription,
                    target: SessionTarget,
                    trust: (ByteArray) -> Trust,
                    onEvent: (ViewerEvent) -> Unit,
                ): Pair<ViewerSessionHandle?, CoreError?> = null to CoreError.Internal
            },
            cable = object : UsbCabling {
                override val events: SharedFlow<CableEvent> = MutableSharedFlow()

                override fun open(): CableOpening = CableOpening.NotAttached
            },
        ),
    )

    @Test
    fun `should need an update when the computer runs Osplay 0 1`() {
        assertEquals(DeviceState.NeedsUpdate, model.stateOf(device(minor = 0)))
    }

    @Test
    fun `should pair on first connection with a current computer`() {
        assertEquals(DeviceState.New, model.stateOf(device(minor = 2)))
    }

    @Test
    fun `should not connect to another major protocol version`() {
        assertEquals(DeviceState.Incompatible, model.stateOf(device(minor = 2, version = 2)))
    }
}
