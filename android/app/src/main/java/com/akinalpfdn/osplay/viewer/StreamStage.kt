package com.akinalpfdn.osplay.viewer

import android.graphics.Bitmap
import android.view.Surface
import com.akinalpfdn.osplay.video.VideoDecoding
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** The size of the stream on screen and whether the host draws the cursor separately. */
data class StreamShape(val width: Int, val height: Int, val separateCursor: Boolean)

/** The host's cursor for separate-cursor streams (Windows hosts), in stream pixels. */
data class CursorState(val image: Bitmap, val x: Int, val y: Int, val hotspotX: Int, val hotspotY: Int)

/**
 * Joins the viewer screen's surface and the session's stream: the decoder starts when both exist
 * and stops when either goes. Its methods are called from the main thread (surface) and the
 * session's I/O thread (stream, frames).
 */
class StreamStage(
    private val makeDecoder: (surface: Surface, shape: StreamShape, needsKeyframe: () -> Unit) -> VideoDecoding,
) {
    private val lock = Any()
    private var surface: Surface? = null
    private var shape: StreamShape? = null
    private var decoder: VideoDecoding? = null
    private var requestKeyframe: () -> Unit = {}
    private var cursorImage: CursorImage? = null
    private val cursorState = MutableStateFlow<CursorState?>(null)
    private val shapeState = MutableStateFlow<StreamShape?>(null)

    private class CursorImage(val shapeId: Int, val bitmap: Bitmap, val hotspotX: Int, val hotspotY: Int)

    /** The stream's shape while one runs; the viewer screen fits the video into it. */
    val stream: StateFlow<StreamShape?> = shapeState.asStateFlow()

    val cursor: StateFlow<CursorState?> = cursorState.asStateFlow()

    fun surfaceCreated(created: Surface) = update { surface = created }

    fun surfaceDestroyed() = update { surface = null }

    fun streamStarted(started: StreamShape, keyframe: () -> Unit) = update {
        shape = started
        requestKeyframe = keyframe
        shapeState.value = started
    }

    fun streamStopped() {
        update {
            shape = null
            shapeState.value = null
        }
        cursorState.value = null
        cursorImage = null
    }

    fun decode(data: ByteArray, keyframe: Boolean, captureTimeUs: Long) {
        synchronized(lock) { decoder }?.decode(data, keyframe, captureTimeUs)
    }

    fun setCursorShape(shapeId: Int, width: Int, height: Int, hotspotX: Int, hotspotY: Int, bgra: ByteArray) {
        if (bgra.size < width * height * 4) return
        val pixels = IntArray(width * height) { i ->
            // Premultiplied BGRA to ARGB.
            val b = bgra[i * 4].toInt() and 0xFF
            val g = bgra[i * 4 + 1].toInt() and 0xFF
            val r = bgra[i * 4 + 2].toInt() and 0xFF
            val a = bgra[i * 4 + 3].toInt() and 0xFF
            (a shl 24) or (r shl 16) or (g shl 8) or b
        }
        val bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888).apply { isPremultiplied = true }
        cursorImage = CursorImage(shapeId, bitmap, hotspotX, hotspotY)
    }

    fun moveCursor(visible: Boolean, shapeId: Int, x: Int, y: Int) {
        val image = cursorImage
        cursorState.value = if (visible && image != null && image.shapeId == shapeId) {
            CursorState(image.bitmap, x, y, image.hotspotX, image.hotspotY)
        } else {
            null
        }
    }

    /** Applies a change and starts or stops the decoder to match. */
    private fun update(change: () -> Unit) {
        val stale: VideoDecoding?
        val start: Pair<Surface, StreamShape>?
        synchronized(lock) {
            change()
            stale = decoder
            decoder = null
            start = surface?.let { s -> shape?.let { s to it } }
        }
        stale?.release()
        if (start != null) {
            val keyframe = requestKeyframe
            val created = makeDecoder(start.first, start.second, keyframe)
            synchronized(lock) { decoder = created }
            keyframe()
        }
    }
}
