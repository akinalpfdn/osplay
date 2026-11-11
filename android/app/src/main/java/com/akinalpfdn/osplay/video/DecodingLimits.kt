package com.akinalpfdn.osplay.video

import android.media.MediaCodecList
import android.media.MediaFormat
import kotlin.math.roundToInt

/** The largest stream this device announces it decodes (Capabilities: max_width, max_height, max_refresh_hz). */
data class DecodingLimits(val maxWidth: Int, val maxHeight: Int, val maxRefreshHz: Int) {
    companion object {
        /** Phones and tablets: their decoders take any desktop a computer sends them. */
        val Generous = DecodingLimits(maxWidth = 7680, maxHeight = 4320, maxRefreshHz = 120)

        private const val FALLBACK_REFRESH_HZ = 30
        private val LADDER = listOf(2560 to 1440, 1920 to 1080, 1280 to 720)

        /**
         * A TV's limits: the largest size, from its own mode down a ladder of common ones, that its
         * decoder [plays] at the mode's frame rate, failing that at 30 fps. A single size and rate is
         * all Capabilities hold, and a limit of 30 Hz would hold every stream to 30 fps; many TV chips
         * decode 1080p at 60 Hz but 4K only at 30 Hz.
         */
        fun forMonitor(width: Int, height: Int, refreshHz: Int, plays: (Int, Int, Int) -> Boolean): DecodingLimits {
            val sizes = (listOf(width to height) + LADDER.map { box -> fit(width, height, box) })
                .filter { (w, h) -> w <= width && h <= height }
                .distinct()
            for (rate in listOf(refreshHz, FALLBACK_REFRESH_HZ).filter { it in 1..refreshHz }.distinct()) {
                sizes.firstOrNull { (w, h) -> plays(w, h, rate) }?.let { (w, h) -> return DecodingLimits(w, h, rate) }
            }
            val (smallW, smallH) = sizes.last()
            return DecodingLimits(smallW, smallH, FALLBACK_REFRESH_HZ.coerceAtMost(refreshHz))
        }

        /** The monitor's shape inside [box] (its longer side against the box's longer one), both sides even. */
        private fun fit(width: Int, height: Int, box: Pair<Int, Int>): Pair<Int, Int> {
            val long = maxOf(width, height).toDouble()
            val short = minOf(width, height).toDouble()
            val scale = minOf(maxOf(box.first, box.second) / long, minOf(box.first, box.second) / short, 1.0)
            val even = { value: Double -> ((value * scale) / 2).roundToInt() * 2 }
            return if (width >= height) even(long) to even(short) else even(short) to even(long)
        }
    }
}

/** Whether the H.264 decoder Osplay uses plays this size at this frame rate (MediaCodec's own figures). */
object MediaCodecSupport {
    fun plays(width: Int, height: Int, frameRate: Int): Boolean {
        val decoder = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.firstOrNull { info ->
            !info.isEncoder && info.supportedTypes.any { it.equals(MediaFormat.MIMETYPE_VIDEO_AVC, ignoreCase = true) }
        } ?: return false
        val video = decoder.getCapabilitiesForType(MediaFormat.MIMETYPE_VIDEO_AVC).videoCapabilities ?: return false
        return video.areSizeAndRateSupported(width, height, frameRate.toDouble())
    }
}
