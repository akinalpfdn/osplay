package com.akinalpfdn.osplay.viewer

import android.os.Handler
import android.os.Looper
import android.util.Log
import com.akinalpfdn.osplay.core.CoreError
import com.akinalpfdn.osplay.core.Identity
import com.akinalpfdn.osplay.core.Native
import com.akinalpfdn.osplay.core.Protocol
import com.akinalpfdn.osplay.core.RoleSwapAnswer
import com.akinalpfdn.osplay.core.SessionListener
import com.akinalpfdn.osplay.core.SessionState
import com.akinalpfdn.osplay.core.Trust
import java.util.concurrent.Executors

/** The monitor this device lends to a host (ViewerMonitors). */
data class MonitorDescription(
    val widthPixels: Int,
    val heightPixels: Int,
    val refreshMillihertz: Int,
    val physicalWidthMillimeters: Int,
    val physicalHeightMillimeters: Int,
    val scalePercent: Int,
    val name: String,
) {
    val isLandscape: Boolean get() = widthPixels > heightPixels
}

/** What the stats overlay shows for the stream. */
data class StatsFigures(
    val framesPerSecond: Int,
    val bitrateKbps: Int,
    val roundTripMilliseconds: Int,
    val lossPercent: Double,
    /** Capture to delivery; null until the clocks are compared. */
    val latencyMilliseconds: Int?,
)

/** What a session reports, on the main thread. */
sealed interface ViewerEvent {
    data class State(val state: SessionState, val failure: CoreError?) : ViewerEvent

    data class Peer(val name: String) : ViewerEvent

    data class PairingCode(val code: Int) : ViewerEvent

    class Paired(val publicKey: ByteArray) : ViewerEvent

    class Seen(val publicKey: ByteArray) : ViewerEvent

    data class Statistics(val figures: StatsFigures?) : ViewerEvent
}

/** A running viewer session, as the view model sees it; a fake in tests. */
interface ViewerSessionHandle {
    fun confirmPairing(accepted: Boolean)

    /** Ends the session; [ViewerEvent.State] reports CLOSED. */
    fun stop()

    /** Ends the session telling the host this device is going away for a while. */
    fun stopForSleep()
}

/** Starts sessions; a fake in tests. */
interface ViewerSessionMaking {
    /** Null with the error when the core refused to start one. */
    fun start(
        identity: Identity,
        deviceName: String,
        monitor: MonitorDescription,
        host: String,
        port: Int,
        trust: (ByteArray) -> Trust,
        onEvent: (ViewerEvent) -> Unit,
    ): Pair<ViewerSessionHandle?, CoreError?>
}

/** Sessions through the core, showing their stream on [stage]. */
class CoreViewerSessions(private val stage: StreamStage) : ViewerSessionMaking {
    override fun start(
        identity: Identity,
        deviceName: String,
        monitor: MonitorDescription,
        host: String,
        port: Int,
        trust: (ByteArray) -> Trust,
        onEvent: (ViewerEvent) -> Unit,
    ): Pair<ViewerSessionHandle?, CoreError?> {
        val session = CoreViewerSession(stage, trust, onEvent)
        val handle = LongArray(1)
        val result = Native.viewerCreate(
            listener = session,
            deviceName = deviceName.encodeToByteArray(),
            secretKey = identity.secretKey,
            monitor = intArrayOf(
                monitor.widthPixels, monitor.heightPixels, monitor.refreshMillihertz,
                monitor.physicalWidthMillimeters, monitor.physicalHeightMillimeters, monitor.scalePercent,
            ),
            monitorName = monitor.name.encodeToByteArray(),
            capabilities = intArrayOf(MAX_STREAMS, MAX_WIDTH, MAX_HEIGHT, MAX_REFRESH_HZ),
            host = host.encodeToByteArray(),
            port = port,
            outHandle = handle,
        )
        val error = CoreError.of(result)
        if (error != null) return null to error
        session.attach(handle[0])
        return session to null
    }

    private companion object {
        const val MAX_STREAMS = 1
        const val MAX_WIDTH = 7680
        const val MAX_HEIGHT = 4320
        const val MAX_REFRESH_HZ = 120
    }
}

/**
 * One core session. Callbacks arrive on the core's I/O thread: frames and cursor go straight to the
 * stage, everything else to the main thread. The session is destroyed off the I/O thread once it
 * closed (destroying joins that thread).
 */
private class CoreViewerSession(
    private val stage: StreamStage,
    private val trust: (ByteArray) -> Trust,
    private val onEvent: (ViewerEvent) -> Unit,
) : SessionListener, ViewerSessionHandle {
    private val main = Handler(Looper.getMainLooper())
    private val lock = Any()
    private var handle = 0L
    private var streamId = -1
    private var peerKey: ByteArray? = null

    fun attach(created: Long) = synchronized(lock) { handle = created }

    private fun <T> withHandle(command: (Long) -> T) {
        val current = synchronized(lock) { handle }
        if (current != 0L) command(current)
    }

    private fun post(event: ViewerEvent) {
        main.post { onEvent(event) }
    }

    override fun confirmPairing(accepted: Boolean) = withHandle { Native.sessionConfirmPairing(it, accepted) }

    override fun stop() = withHandle { Native.sessionClose(it) }

    override fun stopForSleep() = withHandle { Native.sessionCloseForSleep(it) }

    override fun onState(state: Int, reason: Int) {
        val sessionState = SessionState.of(state)
        if (sessionState == SessionState.Streaming) peerKey?.let { post(ViewerEvent.Seen(it)) }
        if (sessionState == SessionState.Closed) {
            stage.streamStopped()
            val closed = synchronized(lock) { handle.also { handle = 0L } }
            if (closed != 0L) destroyer.execute { Native.sessionDestroy(closed) }
        }
        post(ViewerEvent.State(sessionState, CoreError.of(reason)))
    }

    override fun onPeer(platform: Int, protocolMinor: Int, name: ByteArray) {
        post(ViewerEvent.Peer(name.decodeToString()))
    }

    override fun onTrustQuery(publicKey: ByteArray): Int {
        peerKey = publicKey
        return trust(publicKey).code
    }

    override fun onPairingCode(code: Int, publicKey: ByteArray) = post(ViewerEvent.PairingCode(code))

    override fun onPaired(publicKey: ByteArray) = post(ViewerEvent.Paired(publicKey))

    override fun onStreamConfig(
        streamId: Int,
        monitorId: Int,
        width: Int,
        height: Int,
        refreshMillihertz: Int,
        cursorMode: Int,
        bitrateKbps: Int,
    ) {
        this.streamId = streamId
        stage.streamStarted(StreamShape(width, height, cursorMode == Protocol.CURSOR_SEPARATE)) {
            withHandle { Native.sessionRequestKeyframe(it, streamId) }
        }
    }

    override fun onAccessUnit(streamId: Int, keyframe: Boolean, codecConfig: Boolean, captureTimeUs: Long, data: ByteArray) {
        stage.decode(data, keyframe, captureTimeUs)
    }

    override fun onStats(rttUs: Int, streams: IntArray) {
        val figures = if (streams.size >= STATS_FIELDS) {
            StatsFigures(
                framesPerSecond = streams[1],
                bitrateKbps = streams[2],
                roundTripMilliseconds = rttUs / 1000,
                lossPercent = streams[3] / 10.0,
                latencyMilliseconds = streams[4].takeIf { it > 0 }?.div(1000),
            )
        } else {
            null
        }
        post(ViewerEvent.Statistics(figures))
    }

    override fun onLog(level: Int, message: ByteArray) {
        Log.println(if (level >= LOG_WARNING) Log.WARN else Log.INFO, TAG, message.decodeToString())
    }

    override fun onCursorShape(streamId: Int, shapeId: Int, width: Int, height: Int, hotspotX: Int, hotspotY: Int, bgra: ByteArray) {
        stage.setCursorShape(shapeId, width, height, hotspotX, hotspotY, bgra)
    }

    override fun onCursorPosition(streamId: Int, visible: Boolean, shapeId: Int, x: Int, y: Int) {
        stage.moveCursor(visible, shapeId, x, y)
    }

    override fun onStreamStopped(streamId: Int) = stage.streamStopped()

    override fun onRoleSwapRequest(publicKey: ByteArray) {
        // A tablet cannot extend onto the computer; answered off the callback, as the core requires.
        main.post { withHandle { Native.sessionAnswerRoleSwap(it, RoleSwapAnswer.CannotHost.code) } }
    }

    private companion object {
        const val TAG = "Osplay"
        const val STATS_FIELDS = 5
        const val LOG_WARNING = 3
        val destroyer = Executors.newSingleThreadExecutor()
    }
}
