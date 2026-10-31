package com.akinalpfdn.osplay.storage

import com.akinalpfdn.osplay.core.Trust
import kotlinx.serialization.Serializable

/** A computer paired with this device; the key is lowercase hex. */
@Serializable
data class TrustedPeer(
    val publicKey: String,
    val name: String,
    /** A revoked computer is refused without a pairing code until it is removed. */
    val revoked: Boolean = false,
    val lastSeenMillis: Long = 0,
)

/** The paired computers and the trust decision for an incoming key. Pure; [TrustCache] stores it. */
@Serializable
data class PeerBook(val peers: List<TrustedPeer> = emptyList()) {
    fun trust(publicKey: String): Trust {
        val peer = peers.firstOrNull { it.publicKey == publicKey } ?: return Trust.Unknown
        return if (peer.revoked) Trust.Blocked else Trust.Trusted
    }

    fun paired(publicKey: String, name: String, nowMillis: Long): PeerBook =
        PeerBook(peers.filterNot { it.publicKey == publicKey } + TrustedPeer(publicKey, name, false, nowMillis))

    fun seen(publicKey: String, nowMillis: Long): PeerBook =
        PeerBook(peers.map { if (it.publicKey == publicKey) it.copy(lastSeenMillis = nowMillis) else it })

    fun revoke(publicKey: String): PeerBook =
        PeerBook(peers.map { if (it.publicKey == publicKey) it.copy(revoked = true) else it })

    fun remove(publicKey: String): PeerBook = PeerBook(peers.filterNot { it.publicKey == publicKey })
}
