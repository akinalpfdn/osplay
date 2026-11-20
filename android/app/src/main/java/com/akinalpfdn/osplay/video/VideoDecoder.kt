package com.akinalpfdn.osplay.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import java.util.ArrayDeque

/** Decodes one H.264 stream onto a surface; a fake in tests. */
interface VideoDecoding {
    /** Annex B access unit, in order. Called on the session's I/O thread. */
    fun decode(data: ByteArray, keyframe: Boolean, captureTimeUs: Long)

    fun release()
}

/**
 * How much of the low-latency setup a decoder gets. After each failure the next setup drops one more
 * thing, for decoders that reject part of it. Every stream starts again with the full setup: a TV
 * whose video hardware was busy for a while (MediaTek's OMX.MS.AVC.Decoder after a forced stop)
 * fails with every setup, and keeping the simplest one would cost the low latency afterwards.
 */
enum class DecoderSetup(
    val surfaceColorFormat: Boolean,
    val lowLatency: Boolean,
    val steadyTimestamps: Boolean,
) {
    Full(surfaceColorFormat = true, lowLatency = true, steadyTimestamps = false),
    NoColorFormat(surfaceColorFormat = false, lowLatency = true, steadyTimestamps = false),
    NoLowLatency(surfaceColorFormat = false, lowLatency = false, steadyTimestamps = false),
    /** Timestamps one frame apart instead of capture times, for decoders that want them even. */
    SteadyTimestamps(surfaceColorFormat = false, lowLatency = false, steadyTimestamps = true),
}

/**
 * MediaCodec in low-latency mode on its own thread. Every decoded frame is rendered at once. Before
 * the first keyframe, and after any codec error until the next one, frames are dropped and
 * [onNeedsKeyframe] asks the host for a keyframe.
 */
class MediaCodecDecoder(
    private val surface: Surface,
    private val width: Int,
    private val height: Int,
    private val frameRate: Int,
    private val onNeedsKeyframe: () -> Unit,
    private val onFrame: (captureTimeUs: Long) -> Unit,
) : VideoDecoding {
    private val thread = HandlerThread("OsplayDecoder").apply { start() }
    private val handler = Handler(thread.looper)
    private val lock = Any()
    private val units = ArrayDeque<AccessUnit>()
    private val freeInputs = ArrayDeque<Int>()
    private var codec: MediaCodec? = null
    private var waitingForKeyframe = true
    private var released = false
    private var nextTimestampUs = 0L
    private var setup = DecoderSetup.Full
    private var setupWorks = false
    private var decodedFrames = 0
    private var restartAtMillis = 0L
    private val feedLater = Runnable { feed() }
    private var frameUs = 0L

    private class AccessUnit(val data: ByteArray, val captureTimeUs: Long)

    override fun decode(data: ByteArray, keyframe: Boolean, captureTimeUs: Long) {
        val stillWaiting = synchronized(lock) {
            if (released) return
            if (waitingForKeyframe && !keyframe) {
                true
            } else {
                if (waitingForKeyframe) {
                    waitingForKeyframe = false
                    units.clear()
                }
                units.addLast(AccessUnit(data, captureTimeUs))
                false
            }
        }
        if (stillWaiting) {
            // The keyframe asked for may have been lost; a still screen sends no other. The core
            // spaces the requests out.
            onNeedsKeyframe()
            return
        }
        handler.post { feed() }
    }

    override fun release() {
        synchronized(lock) {
            released = true
            units.clear()
        }
        handler.post {
            stopCodec()
            thread.quitSafely()
        }
    }

    private fun feed() {
        // After a failure the TV's video hardware is released in the background; a codec started
        // at once may find it still busy.
        val wait = restartAtMillis - SystemClock.elapsedRealtime()
        if (codec == null && wait > 0) {
            handler.removeCallbacks(feedLater)
            handler.postDelayed(feedLater, wait)
            return
        }
        val codec = codec ?: startCodec() ?: return
        while (true) {
            val index: Int
            val unit: AccessUnit
            synchronized(lock) {
                if (released || units.isEmpty() || freeInputs.isEmpty()) return
                index = freeInputs.removeFirst()
                unit = units.removeFirst()
            }
            try {
                val buffer = codec.getInputBuffer(index) ?: continue
                if (unit.data.size > buffer.capacity()) {
                    failed()
                    return
                }
                buffer.clear()
                buffer.put(unit.data)
                val timestamp = if (setup.steadyTimestamps) nextTimestampUs.also { nextTimestampUs += frameUs } else unit.captureTimeUs
                codec.queueInputBuffer(index, 0, unit.data.size, timestamp, 0)
            } catch (error: IllegalStateException) {
                failed()
                return
            }
        }
    }

    private fun startCodec(): MediaCodec? {
        val setup = setup
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
            // TV decoders (MediaTek's) set up their display timing from it and may fail without it.
            setInteger(MediaFormat.KEY_FRAME_RATE, frameRate)
            if (setup.lowLatency) {
                setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
                setInteger(MediaFormat.KEY_PRIORITY, 0) // realtime
            }
            if (setup.surfaceColorFormat) {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            }
        }
        return try {
            MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC).also { created ->
                created.setCallback(callback, handler)
                created.configure(format, surface, null, 0)
                created.start()
                codec = created
                nextTimestampUs = 0L
                frameUs = 1_000_000L / frameRate.coerceAtLeast(1)
                Log.i(TAG, "Decoder ${created.name} started with setup $setup at ${width}x$height")
            }
        } catch (error: Exception) {
            // createDecoderByType, configure and start report failures with several exception types.
            synchronized(lock) { waitingForKeyframe = true }
            onNeedsKeyframe()
            null
        }
    }

    private fun stopCodec() {
        codec?.let { runCatching { it.stop() }; it.release() }
        codec = null
        synchronized(lock) { freeInputs.clear() }
    }

    /** Starts over at the next keyframe with a fresh codec, one setup simpler unless one already worked. */
    private fun failed() {
        val failedSetup = setup
        if (!setupWorks && failedSetup.ordinal < DecoderSetup.entries.lastIndex) {
            setup = DecoderSetup.entries[failedSetup.ordinal + 1]
        }
        Log.w(TAG, "Decoder failed with setup $failedSetup; next $setup")
        stopCodec()
        restartAtMillis = SystemClock.elapsedRealtime() + RESTART_DELAY_MILLIS
        synchronized(lock) {
            waitingForKeyframe = true
            units.clear()
        }
        onNeedsKeyframe()
    }

    private val callback = object : MediaCodec.Callback() {
        // The codec is stopped and released from posted work, never inside its own callback.
        override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
            if (codec !== this@MediaCodecDecoder.codec) return // a replaced codec's buffer
            synchronized(lock) { freeInputs.addLast(index) }
            handler.post { feed() }
        }

        override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
            codec.releaseOutputBuffer(index, true)
            if (info.size > 0) {
                decodedFrames++
                if (!setupWorks && decodedFrames >= WORKING_FRAMES) {
                    setupWorks = true
                    Log.i(TAG, "Decoder setup $setup works")
                }
                onFrame(info.presentationTimeUs)
            }
        }

        override fun onError(codec: MediaCodec, error: MediaCodec.CodecException) {
            Log.w(TAG, "Decoder error ${error.diagnosticInfo} (recoverable ${error.isRecoverable}, transient ${error.isTransient})")
            // A late report from a codec already replaced must not stop its successor.
            handler.post { if (codec === this@MediaCodecDecoder.codec) failed() }
        }

        override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) = Unit
    }

    private companion object {
        const val TAG = "OsplayDecoder"
        /** How long a failed codec's hardware gets to come free before the next one starts. */
        const val RESTART_DELAY_MILLIS = 1_000L
        /** Frames a setup must decode before it counts as working. */
        const val WORKING_FRAMES = 120
    }
}
