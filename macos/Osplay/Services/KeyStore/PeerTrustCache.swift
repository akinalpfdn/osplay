import CoreBridge
import Foundation

/// The paired devices in memory: the core's trust callback runs on its I/O thread and must answer
/// without touching the Keychain. Writes go through to the store. `@unchecked`: every field is
/// guarded by `lock`.
final class PeerTrustCache: @unchecked Sendable {
    private let store: any KeyStoring
    private let lock = NSLock()
    private var book = PeerBook()
    private var lastQueried: [UInt8]?

    init(store: any KeyStoring) {
        self.store = store
    }

    /// Reloads from the store.
    func load() throws(KeyStoreError) {
        let loaded = try store.peers()
        lock.withLock { book = loaded }
    }

    var peers: [TrustedPeer] { lock.withLock { book.peers } }

    /// For the core's `trust` closure. Remembers the key so the host can tell viewers apart.
    func trust(_ publicKey: [UInt8]) -> TrustDecision {
        lock.withLock {
            lastQueried = publicKey
            return book.trust(publicKey)
        }
    }

    /// The key of the most recent trust query: the current session's peer.
    var currentPeer: [UInt8]? { lock.withLock { lastQueried } }

    func paired(_ publicKey: [UInt8], name: String) throws(KeyStoreError) {
        try change { $0.trust(publicKey, name: name, at: Date()) }
    }

    func seen(_ publicKey: [UInt8]) throws(KeyStoreError) {
        try change { $0.seen(publicKey, at: Date()) }
    }

    func revoke(_ publicKey: [UInt8]) throws(KeyStoreError) {
        try change { $0.revoke(publicKey) }
    }

    func remove(_ publicKey: [UInt8]) throws(KeyStoreError) {
        try change { $0.remove(publicKey) }
    }

    private func change(_ update: (inout PeerBook) -> Void) throws(KeyStoreError) {
        let changed = lock.withLock {
            update(&book)
            return book
        }
        try store.save(changed)
    }
}
