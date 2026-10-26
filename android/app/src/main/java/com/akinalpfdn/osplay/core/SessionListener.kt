package com.akinalpfdn.osplay.core

/**
 * The core's session callbacks (osplay_session_callbacks), called on the session's I/O thread.
 * [onTrustQuery] must answer from memory: the I/O thread waits for it.
 */
interface SessionListener {
    fun onState(state: Int, reason: Int)

    fun onPeer(platform: Int, protocolMinor: Int, name: ByteArray)

    fun onTrustQuery(publicKey: ByteArray): Int

    fun onPairingCode(code: Int, publicKey: ByteArray)

    fun onPaired(publicKey: ByteArray)

    fun onStreamConfig(
        streamId: Int,
        monitorId: Int,
        width: Int,
        height: Int,
        refreshMillihertz: Int,
        cursorMode: Int,
        bitrateKbps: Int,
    )

    fun onAccessUnit(streamId: Int, keyframe: Boolean, codecConfig: Boolean, captureTimeUs: Long, data: ByteArray)

    /** Per stream: id, frames per second, bitrate (kbit/s), loss (permille), latency (µs). */
    fun onStats(rttUs: Int, streams: IntArray)

    fun onLog(level: Int, message: ByteArray)

    fun onCursorShape(streamId: Int, shapeId: Int, width: Int, height: Int, hotspotX: Int, hotspotY: Int, bgra: ByteArray)

    fun onCursorPosition(streamId: Int, visible: Boolean, shapeId: Int, x: Int, y: Int)

    fun onStreamStopped(streamId: Int)

    fun onRoleSwapRequest(publicKey: ByteArray)
}
