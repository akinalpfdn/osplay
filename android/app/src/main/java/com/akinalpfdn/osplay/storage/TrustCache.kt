package com.akinalpfdn.osplay.storage

import com.akinalpfdn.osplay.core.Identity
import com.akinalpfdn.osplay.core.Trust
import com.akinalpfdn.osplay.core.toHexString

/**
 * The paired computers in memory: the core's trust query runs on its I/O thread and must answer
 * without storage access. Writes go through to the store.
 */
class TrustCache(private val store: KeyStoring) {
    private val lock = Any()
    private var book = PeerBook()

    fun load() {
        val loaded = store.peers()
        synchronized(lock) { book = loaded }
    }

    val peers: List<TrustedPeer> get() = synchronized(lock) { book.peers }

    fun trust(publicKey: ByteArray): Trust = synchronized(lock) { book.trust(publicKey.toHexString()) }

    fun isPaired(deviceId: String): Boolean = peers.any { !it.revoked && deviceIdOf(it) == deviceId }

    /** The paired computer's name, or null. */
    fun nameOf(deviceId: String): String? = peers.firstOrNull { deviceIdOf(it) == deviceId }?.name

    fun paired(publicKey: ByteArray, name: String) = change { it.paired(publicKey.toHexString(), name, now()) }

    fun seen(publicKey: ByteArray) = change { it.seen(publicKey.toHexString(), now()) }

    fun revoke(peer: TrustedPeer) = change { it.revoke(peer.publicKey) }

    fun remove(peer: TrustedPeer) = change { it.remove(peer.publicKey) }

    private fun change(update: (PeerBook) -> PeerBook) {
        val changed = synchronized(lock) {
            book = update(book)
            book
        }
        store.save(changed)
    }

    private fun now() = System.currentTimeMillis()

    private companion object {
        fun deviceIdOf(peer: TrustedPeer): String = Identity.deviceId(peer.publicKey.hexToBytes())

        fun String.hexToBytes(): ByteArray = chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }
}
