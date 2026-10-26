package com.akinalpfdn.osplay.screen

import android.content.Context
import android.hardware.display.DisplayManager
import android.view.Display
import android.view.WindowManager
import com.akinalpfdn.osplay.viewer.MonitorDescription
import kotlin.math.roundToInt

/** The monitor this device lends to a host; a fake in tests. */
interface ScreenDescribing {
    fun monitor(): MonitorDescription?
}

/** The whole screen in the orientation it is held, in physical pixels. */
class AndroidScreen(private val context: Context) : ScreenDescribing {
    override fun monitor(): MonitorDescription? {
        val bounds = context.getSystemService(WindowManager::class.java)?.maximumWindowMetrics?.bounds ?: return null
        val display = context.getSystemService(DisplayManager::class.java)?.getDisplay(Display.DEFAULT_DISPLAY)
        val metrics = context.resources.displayMetrics
        return describe(
            widthPixels = bounds.width(),
            heightPixels = bounds.height(),
            refreshRate = display?.refreshRate ?: 0f,
            xdpi = metrics.xdpi,
            ydpi = metrics.ydpi,
            densityDpi = metrics.densityDpi,
            name = android.os.Build.MODEL,
        )
    }

    companion object {
        private const val DEFAULT_REFRESH = 60f
        private const val MAX_REFRESH = 120f
        private const val BASELINE_DPI = 160

        /** The monitor as the protocol takes it (PROTOCOL.md ViewerMonitors). */
        fun describe(
            widthPixels: Int,
            heightPixels: Int,
            refreshRate: Float,
            xdpi: Float,
            ydpi: Float,
            densityDpi: Int,
            name: String,
        ): MonitorDescription {
            val refresh = if (refreshRate > 0f) refreshRate.coerceAtMost(MAX_REFRESH) else DEFAULT_REFRESH
            return MonitorDescription(
                widthPixels = widthPixels,
                heightPixels = heightPixels,
                refreshMillihertz = (refresh * 1000).roundToInt(),
                physicalWidthMillimeters = millimeters(widthPixels, xdpi),
                physicalHeightMillimeters = millimeters(heightPixels, ydpi),
                scalePercent = (densityDpi * 100 / BASELINE_DPI).coerceIn(100, 400),
                name = ProtocolText.name(name),
            )
        }

        private fun millimeters(pixels: Int, dpi: Float): Int =
            if (dpi > 0f) (pixels / dpi * 25.4f).roundToInt().coerceIn(0, 10_000) else 0
    }
}

/** Text as the protocol carries it (PROTOCOL.md "Conventions"). */
object ProtocolText {
    private const val NAME_BYTES = 64

    /** No control characters, at most 64 bytes of UTF-8, cut between characters. */
    fun name(text: String): String {
        val result = StringBuilder()
        var offset = 0
        while (offset < text.length) {
            val codePoint = text.codePointAt(offset)
            val character = String(Character.toChars(codePoint))
            offset += Character.charCount(codePoint)
            if (Character.isISOControl(codePoint) || codePoint in 0x80..0x9F) continue
            if (result.toString().encodeToByteArray().size + character.encodeToByteArray().size > NAME_BYTES) break
            result.append(character)
        }
        return result.toString()
    }
}
