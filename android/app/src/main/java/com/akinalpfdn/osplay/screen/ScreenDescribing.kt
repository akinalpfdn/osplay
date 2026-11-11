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

/** The whole screen in the orientation it is held, in physical pixels; on a TV, the panel's own mode. */
class AndroidScreen(private val context: Context, private val television: Boolean = context.isTelevision()) :
    ScreenDescribing {
    override fun monitor(): MonitorDescription? {
        val bounds = context.getSystemService(WindowManager::class.java)?.maximumWindowMetrics?.bounds ?: return null
        val display = context.getSystemService(DisplayManager::class.java)?.getDisplay(Display.DEFAULT_DISPLAY)
        val metrics = context.resources.displayMetrics
        if (television) {
            // A TV draws its interface at 1080p or less while the panel and the video plane run at
            // the display mode (4K on a 4K TV); the stream's SurfaceView goes to the video plane.
            val mode = display?.mode
            return describeTelevision(
                panelWidth = mode?.physicalWidth ?: bounds.width(),
                panelHeight = mode?.physicalHeight ?: bounds.height(),
                refreshRate = mode?.refreshRate ?: display?.refreshRate ?: 0f,
                interfaceWidth = maxOf(bounds.width(), bounds.height()),
                densityDpi = metrics.densityDpi,
                name = android.os.Build.MODEL,
            )
        }
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
        /** A 1080p TV interface at xhdpi, which a desktop shows at 100% on such a TV. */
        private const val TV_BASELINE_DPI = 320

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

        /**
         * A TV as the protocol takes it: the panel's mode, in landscape; no physical size, since
         * TVs report none that can be trusted; and the scale its own interface uses, so a 4K TV gets
         * a desktop that looks like 1080p (200%) and a 1080p TV one at full size.
         */
        fun describeTelevision(
            panelWidth: Int,
            panelHeight: Int,
            refreshRate: Float,
            interfaceWidth: Int,
            densityDpi: Int,
            name: String,
        ): MonitorDescription {
            val refresh = if (refreshRate > 0f) refreshRate.coerceAtMost(MAX_REFRESH) else DEFAULT_REFRESH
            val width = maxOf(panelWidth, panelHeight)
            val scale = if (interfaceWidth > 0) width * densityDpi * 100 / (interfaceWidth * TV_BASELINE_DPI) else 100
            return MonitorDescription(
                widthPixels = width,
                heightPixels = minOf(panelWidth, panelHeight),
                refreshMillihertz = (refresh * 1000).roundToInt(),
                physicalWidthMillimeters = 0,
                physicalHeightMillimeters = 0,
                scalePercent = scale.coerceIn(100, 400),
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
