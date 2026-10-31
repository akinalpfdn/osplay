package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.core.Advertisement
import com.akinalpfdn.osplay.core.Platform
import com.akinalpfdn.osplay.discovery.CombinedDiscovery
import com.akinalpfdn.osplay.discovery.DiscoveredDevice
import com.akinalpfdn.osplay.discovery.DiscoveryEvent
import com.akinalpfdn.osplay.discovery.PeerDiscovering
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class) // UnconfinedTestDispatcher: events reach the collector at once
class CombinedDiscoveryTest {
    private class FakeSource : PeerDiscovering {
        val events = MutableSharedFlow<DiscoveryEvent>()

        override fun browse(): Flow<DiscoveryEvent> = events
    }

    private fun found(label: String) = DiscoveryEvent.Found(
        DiscoveredDevice(label, Advertisement(1, 2, Platform.of(0), 1, "ab", "PC"), "192.168.137.1", 51903),
    )

    @Test
    fun `should keep a computer listed while another source still sees it`() = runTest(UnconfinedTestDispatcher()) {
        val nsd = FakeSource()
        val own = FakeSource()
        val received = mutableListOf<DiscoveryEvent>()
        val job = launch { CombinedDiscovery(listOf(nsd, own)).browse().collect { received += it } }

        nsd.events.emit(found("pc"))
        own.events.emit(found("pc"))
        nsd.events.emit(DiscoveryEvent.Lost("pc"))
        assertEquals(listOf(found("pc"), found("pc")), received)

        own.events.emit(DiscoveryEvent.Lost("pc"))
        assertEquals(DiscoveryEvent.Lost("pc"), received.last())
        job.cancel()
    }

    @Test
    fun `should keep browsing when one source fails`() = runTest(UnconfinedTestDispatcher()) {
        val nsd = FakeSource()
        val own = FakeSource()
        val received = mutableListOf<DiscoveryEvent>()
        val job = launch { CombinedDiscovery(listOf(nsd, own)).browse().collect { received += it } }

        nsd.events.emit(DiscoveryEvent.Failed(3))
        own.events.emit(found("pc"))
        assertEquals(listOf(found("pc")), received)
        job.cancel()
    }

    @Test
    fun `should fail and end once every source failed`() = runTest(UnconfinedTestDispatcher()) {
        val nsd = FakeSource()
        val own = FakeSource()
        val received = mutableListOf<DiscoveryEvent>()
        val job = launch { received += CombinedDiscovery(listOf(nsd, own)).browse().toList() }

        own.events.emit(found("pc"))
        nsd.events.emit(DiscoveryEvent.Failed(3))
        own.events.emit(DiscoveryEvent.Failed(4))
        assertEquals(listOf(found("pc"), DiscoveryEvent.Lost("pc"), DiscoveryEvent.Failed(4)), received)
        assertEquals(true, job.isCompleted)
    }
}
