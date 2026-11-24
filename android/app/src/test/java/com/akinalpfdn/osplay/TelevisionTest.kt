package com.akinalpfdn.osplay

import com.akinalpfdn.osplay.screen.AndroidScreen
import com.akinalpfdn.osplay.video.DecodingLimits
import org.junit.Assert.assertEquals
import org.junit.Test

class TelevisionTest {
    @Test
    fun `should offer a 4K TV with a 1080p interface as its panel at 200 percent and no size`() {
        val tv = AndroidScreen.describeTelevision(
            panelWidth = 3840, panelHeight = 2160, refreshRate = 60f, interfaceWidth = 1920, densityDpi = 320,
            name = "Vestel",
        )
        assertEquals(3840, tv.widthPixels)
        assertEquals(2160, tv.heightPixels)
        assertEquals(60_000, tv.refreshMillihertz)
        assertEquals(0, tv.physicalWidthMillimeters)
        assertEquals(0, tv.physicalHeightMillimeters)
        assertEquals(200, tv.scalePercent)
    }

    @Test
    fun `should offer a 1080p TV at full size`() {
        val tv = AndroidScreen.describeTelevision(1920, 1080, 50f, interfaceWidth = 1920, densityDpi = 320, name = "TV")
        assertEquals(100, tv.scalePercent)
        assertEquals(50_000, tv.refreshMillihertz)
    }

    @Test
    fun `should give a 4K interface drawn at 4K the same 200 percent`() {
        val tv = AndroidScreen.describeTelevision(3840, 2160, 60f, interfaceWidth = 3840, densityDpi = 640, name = "TV")
        assertEquals(200, tv.scalePercent)
    }

    @Test
    fun `should announce the TV's mode when its decoder plays it at full rate`() {
        val limits = DecodingLimits.forMonitor(3840, 2160, 60) { _, _, _ -> true }
        assertEquals(DecodingLimits(3840, 2160, 60), limits)
    }

    @Test
    fun `should keep 60 Hz at a smaller size when the decoder plays 4K only at 30 Hz`() {
        val limits = DecodingLimits.forMonitor(3840, 2160, 60) { width, _, rate -> width <= 1920 || rate <= 30 }
        assertEquals(DecodingLimits(1920, 1080, 60), limits)
    }

    @Test
    fun `should step down to 30 Hz only when no size plays at the mode's rate`() {
        val limits = DecodingLimits.forMonitor(3840, 2160, 60) { _, _, rate -> rate <= 30 }
        assertEquals(DecodingLimits(3840, 2160, 30), limits)
    }

    @Test
    fun `should keep the panel's shape on the ladder`() {
        val limits = DecodingLimits.forMonitor(3840, 1600, 60) { width, _, _ -> width <= 1920 }
        assertEquals(DecodingLimits(1920, 800, 60), limits)
    }

    @Test
    fun `should fall back to the smallest size at 30 Hz when nothing plays`() {
        val limits = DecodingLimits.forMonitor(1920, 1080, 60) { _, _, _ -> false }
        assertEquals(DecodingLimits(1280, 720, 30), limits)
    }
}
