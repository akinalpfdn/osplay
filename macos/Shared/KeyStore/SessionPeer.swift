import CoreBridge
import Foundation

/// The key the peer of one session presented, noted from the core's trust query. One per session,
/// so a host with several viewers tells them apart. `@unchecked`: `stored` is guarded by `lock`.
final class SessionPeer: @unchecked Sendable {
    private let lock = NSLock()
    private var stored: [UInt8]?

    /// For the core's `trust` closure: notes the key and answers from the paired devices.
    func trust(_ publicKey: [UInt8], in cache: PeerTrustCache) -> TrustDecision {
        lock.withLock { stored = publicKey }
        return cache.trust(publicKey)
    }

    /// nil until the peer's handshake reached the trust query.
    var key: [UInt8]? { lock.withLock { stored } }
}
