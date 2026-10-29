package com.akinalpfdn.osplay.core

/** The shared core's C API through `osplay_jni.cpp`. Text crosses as UTF-8 byte arrays. */
internal object Native {
    init {
        System.loadLibrary("osplay_jni")
    }

    external fun coreVersion(): IntArray

    /** A new device secret key, or null when the random source failed. */
    external fun identityGenerate(): ByteArray?

    external fun identityPublicKey(secretKey: ByteArray): ByteArray?

    external fun identityDeviceId(publicKey: ByteArray): ByteArray?

    /** [result, protocol version, protocol minor, platform, roles]. */
    external fun advertisementFromTxt(
        protocolVersion: ByteArray,
        name: ByteArray,
        deviceId: ByteArray,
        roles: ByteArray,
        os: ByteArray,
        protocolMinor: ByteArray,
    ): IntArray

    /**
     * monitor: width, height, refresh (mHz), physical width and height (mm), scale (%);
     * capabilities: max streams, max width, max height, max refresh (Hz). With [accessoryFd] >= 0
     * the session runs on that USB accessory, which the core owns from here on, instead of [host].
     */
    external fun viewerCreate(
        listener: SessionListener,
        deviceName: ByteArray,
        secretKey: ByteArray,
        monitor: IntArray,
        monitorName: ByteArray,
        capabilities: IntArray,
        host: ByteArray,
        port: Int,
        accessoryFd: Int,
        outHandle: LongArray,
    ): Int

    external fun sessionConfirmPairing(handle: Long, accepted: Boolean): Int

    external fun sessionRequestKeyframe(handle: Long, streamId: Int): Int

    external fun sessionPauseStream(handle: Long, streamId: Int): Int

    external fun sessionResumeStream(handle: Long, streamId: Int): Int

    external fun sessionAnswerRoleSwap(handle: Long, answer: Int): Int

    external fun sessionClose(handle: Long): Int

    external fun sessionCloseForSleep(handle: Long): Int

    /** Joins the session's I/O thread: never call it from a [SessionListener] callback. */
    external fun sessionDestroy(handle: Long)

    external fun clockMicroseconds(): Long
}
