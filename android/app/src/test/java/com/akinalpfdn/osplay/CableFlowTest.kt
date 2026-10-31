package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.app.AppDependencies
import com.akinalpfdn.osplay.app.MainViewModel
import com.akinalpfdn.osplay.app.Screen
import com.akinalpfdn.osplay.app.SettingsStoring
import com.akinalpfdn.osplay.core.CoreError
import com.akinalpfdn.osplay.core.Identity
import com.akinalpfdn.osplay.core.SessionState
import com.akinalpfdn.osplay.core.Trust
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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** The USB cable (TAB-05): waiting, opening Osplay's accessory and what happens when it ends. */
@OptIn(ExperimentalCoroutinesApi::class)
class CableFlowTest {
    private val dispatcher = StandardTestDispatcher()

    private class FakeCable : UsbCabling {
        override val events = MutableSharedFlow<CableEvent>(extraBufferCapacity = 4)
        val openings = ArrayDeque<CableOpening>()
        var opened = 0

        override fun open(): CableOpening {
            opened++
            return openings.removeFirstOrNull() ?: CableOpening.NotAttached
        }
    }

    private class FakeSessions : ViewerSessionMaking {
        val targets = mutableListOf<SessionTarget>()
        var onEvent: (ViewerEvent) -> Unit = {}
        var stops = 0

        override fun start(
            identity: Identity,
            deviceName: String,
            monitor: MonitorDescription,
            target: SessionTarget,
            trust: (ByteArray) -> Trust,
            onEvent: (ViewerEvent) -> Unit,
        ): Pair<ViewerSessionHandle?, CoreError?> {
            targets += target
            this.onEvent = onEvent
            return object : ViewerSessionHandle {
                override fun confirmPairing(accepted: Boolean) = Unit

                override fun stop() {
                    stops++
                }

                override fun stopForSleep() = Unit
            } to null
        }

        fun close(failure: CoreError?) = onEvent(ViewerEvent.State(SessionState.Closed, failure))
    }

    private val cable = FakeCable()
    private val sessions = FakeSessions()
    private lateinit var model: MainViewModel

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
        model = MainViewModel(
            AppDependencies(
                keyStore = object : KeyStoring {
                    override fun identity() = Identity(ByteArray(32))

                    override fun peers() = PeerBook()

                    override fun save(peers: PeerBook) = Unit
                },
                discovery = object : PeerDiscovering {
                    override fun browse(): Flow<DiscoveryEvent> = emptyFlow()
                },
                screen = object : ScreenDescribing {
                    override fun monitor() = MonitorDescription(2560, 1600, 60_000, 250, 160, 200, "Tab")
                },
                settings = object : SettingsStoring {
                    override var deviceName = "Tablet"
                    override var showsStatsOverlay = false
                },
                stage = StreamStage { _, _, _ -> error("no decoder in this test") },
                sessions = sessions,
                cable = cable,
            ),
        )
        model.start()
        dispatcher.scheduler.runCurrent()
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun `should start a session on the accessory once a computer switches this device`() = runTest(dispatcher) {
        model.connectCable()
        assertEquals(Screen.WaitingForCable, model.uiState.value.screen)
        assertTrue(sessions.targets.isEmpty())

        cable.openings += CableOpening.Opened(fd = 42)
        model.accessoryAttached()
        assertEquals(listOf<SessionTarget>(SessionTarget.Cable(42)), sessions.targets)
        assertEquals(Screen.Connecting(""), model.uiState.value.screen)

        sessions.onEvent(ViewerEvent.Peer("Studio Mac"))
        assertEquals(Screen.Connecting("Studio Mac"), model.uiState.value.screen)
    }

    @Test
    fun `should start when Android hands Osplay the accessory on the home screen`() = runTest(dispatcher) {
        cable.openings += CableOpening.Opened(fd = 7)
        model.accessoryAttached()
        assertEquals(listOf<SessionTarget>(SessionTarget.Cable(7)), sessions.targets)
    }

    @Test
    fun `should try again while the previous session still holds the accessory`() = runTest(dispatcher) {
        cable.openings += CableOpening.Busy
        cable.openings += CableOpening.Opened(fd = 9)
        model.connectCable()
        assertTrue(sessions.targets.isEmpty())

        advanceTimeBy(1_500)
        runCurrent()
        assertEquals(listOf<SessionTarget>(SessionTarget.Cable(9)), sessions.targets)
    }

    @Test
    fun `should open the accessory after the user allows it`() = runTest(dispatcher) {
        cable.openings += CableOpening.AskedForPermission
        cable.openings += CableOpening.Opened(fd = 5)
        model.connectCable()
        cable.events.emit(CableEvent.PermissionGranted)
        runCurrent()
        assertEquals(listOf<SessionTarget>(SessionTarget.Cable(5)), sessions.targets)
    }

    @Test
    fun `should wait on the cable again when the computer went away for a while`() = runTest(dispatcher) {
        cable.openings += CableOpening.Opened(fd = 3)
        model.connectCable()
        sessions.close(CoreError.Network) // unplugged
        assertEquals(Screen.WaitingForCable, model.uiState.value.screen)
    }

    @Test
    fun `should not wait on the cable again after a disconnect`() = runTest(dispatcher) {
        cable.openings += CableOpening.Opened(fd = 3)
        model.connectCable()
        model.disconnect()
        sessions.close(CoreError.Network)
        assertEquals(Screen.Home, model.uiState.value.screen)
        assertEquals(1, cable.opened)
    }

    @Test
    fun `should stop waiting on cancel`() = runTest(dispatcher) {
        model.connectCable()
        model.disconnect()
        assertEquals(Screen.Home, model.uiState.value.screen)
        cable.events.emit(CableEvent.PermissionGranted)
        runCurrent()
        assertTrue(sessions.targets.isEmpty())
    }
}
