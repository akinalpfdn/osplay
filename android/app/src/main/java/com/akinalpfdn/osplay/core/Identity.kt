package com.akinalpfdn.osplay.core

/** The device identity: a static Curve25519 key pair; the device ID derives from the public key. */
class Identity(val secretKey: ByteArray) {
    /** Derived on first use, so the view model's tests can hold an identity without the core. */
    val publicKey: ByteArray by lazy { requireNotNull(Native.identityPublicKey(secretKey)) { "invalid secret key" } }

    val deviceId: String get() = deviceId(publicKey)

    companion object {
        /** Null when the system's random source failed. */
        fun generate(): Identity? = Native.identityGenerate()?.let(::Identity)

        /** Lowercase hex of the device ID, as Bonjour records carry it. */
        fun deviceId(publicKey: ByteArray): String =
            requireNotNull(Native.identityDeviceId(publicKey)) { "invalid public key" }.toHexString()
    }
}

/** Lowercase hex, as the protocol writes keys and device IDs. */
fun ByteArray.toHexString(): String = joinToString("") { "%02x".format(it) }
