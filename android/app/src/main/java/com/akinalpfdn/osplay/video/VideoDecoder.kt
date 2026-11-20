package com.akinalpfdn.osplay.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Handler
import android.os.HandlerThread
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
 * How much of the low-latency setup a decoder gets. Some TV decoders (MediaTek's OMX.MS.AVC.Decoder)
 * fail within a second with the full setup; after each failure the next setup drops one more
 * thing, and the one that works is kept for the rest of the process.
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

    private class AccessUnit(val data: ByteArray, val captureTimeUs: Long)

    override fun decode(data: ByteArray, keyframe: Boolean, captureTimeUs: Long) {
        synchronized(lock) {
            if (released) return
            if (waitingForKeyframe) {
                if (!keyframe) return
                waitingForKeyframe = false
                units.clear()
            }
            units.addLast(AccessUnit(data, captureTimeUs))
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
                val timestamp = if (setup.steadyTimestamps) nextTimestampUs.also { nextTimestampUs += FRAME_US } else unit.captureTimeUs
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
        synchronized(lock) {
            waitingForKeyframe = true
            units.clear()
        }
        onNeedsKeyframe()
    }

    private val callback = object : MediaCodec.Callback() {
        // The codec is stopped and released from posted work, never inside its own callback.
        override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
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
            handler.post { failed() }
        }

        override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) = Unit
    }

    private companion object {
        const val TAG = "OsplayDecoder"
        const val FRAME_US = 16_667L
        /** Frames a setup must decode before it counts as working. */
        const val WORKING_FRAMES = 120

        /** Shared by every decoder of the process: a TV that needs a simpler setup keeps it. */
        @Volatile var setup = DecoderSetup.Full
        @Volatile var setupWorks = false
        @Volatile var decodedFrames = 0
    }
}
