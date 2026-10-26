package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.screen.AndroidScreen
import com.akinalpfdn.osplay.screen.ProtocolText
import com.akinalpfdn.osplay.ui.pairingCodeText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ScreenTest {
    @Test
    fun `should offer the screen as held with its density as scale`() {
        val monitor = AndroidScreen.describe(
            widthPixels = 3088, heightPixels = 1440, refreshRate = 120f, xdpi = 500f, ydpi = 500f,
            densityDpi = 600, name = "SM-S918B",
        )
        assertEquals(3088, monitor.widthPixels)
        assertEquals(1440, monitor.heightPixels)
        assertEquals(120_000, monitor.refreshMillihertz)
        assertEquals(375, monitor.scalePercent)
        assertEquals(157, monitor.physicalWidthMillimeters)
        assertTrue(monitor.isLandscape)
    }

    @Test
    fun `should fall back to 60 Hz and clamp the scale the protocol accepts`() {
        val monitor = AndroidScreen.describe(1920, 1200, 0f, 0f, 0f, densityDpi = 1000, name = "Tab")
        assertEquals(60_000, monitor.refreshMillihertz)
        assertEquals(400, monitor.scalePercent)
        assertEquals(0, monitor.physicalWidthMillimeters)
    }

    @Test
    fun `should cut names to 64 bytes between characters and drop control characters`() {
        assertEquals("Tablet", ProtocolText.name("Tab\u0007let\n"))
        val long = ProtocolText.name("ş".repeat(40))
        assertTrue(long.encodeToByteArray().size <= 64)
        assertEquals(32, long.length)
    }

    @Test
    fun `should show the pairing code in two groups of three`() {
        assertEquals("012 345", pairingCodeText(12345))
    }
}
