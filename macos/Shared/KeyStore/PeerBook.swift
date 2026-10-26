import CoreBridge
import Foundation

/// A device this Mac paired with (DEVPLAN: public key, display name, last seen).
struct TrustedPeer: Codable, Sendable, Equatable, Identifiable {
    let publicKey: [UInt8]
    var name: String
    /// Revoked devices are refused without a pairing code until removed.
    var revoked: Bool
    var lastSeen: Date

    var id: [UInt8] { publicKey }
}

/// The paired devices and the trust decision for an incoming key.
struct PeerBook: Codable, Sendable, Equatable {
    private(set) var peers: [TrustedPeer] = []

    func trust(_ publicKey: [UInt8]) -> TrustDecision {
        guard let peer = peers.first(where: { $0.publicKey == publicKey }) else { return .unknown }
        return peer.revoked ? .blocked : .trusted
    }

    /// Stores a completed pairing; pairing again with a revoked device un-revokes it.
    mutating func trust(_ publicKey: [UInt8], name: String, at date: Date) {
        if let index = peers.firstIndex(where: { $0.publicKey == publicKey }) {
            peers[index].name = name
            peers[index].revoked = false
            peers[index].lastSeen = date
        } else {
            peers.append(TrustedPeer(publicKey: publicKey, name: name, revoked: false, lastSeen: date))
        }
    }

    mutating func rename(_ publicKey: [UInt8], to name: String) {
        guard let index = peers.firstIndex(where: { $0.publicKey == publicKey }), !name.isEmpty else { return }
        peers[index].name = name
    }

    mutating func revoke(_ publicKey: [UInt8]) {
        guard let index = peers.firstIndex(where: { $0.publicKey == publicKey }) else { return }
        peers[index].revoked = true
    }

    mutating func remove(_ publicKey: [UInt8]) {
        peers.removeAll { $0.publicKey == publicKey }
    }

    mutating func seen(_ publicKey: [UInt8], at date: Date) {
        guard let index = peers.firstIndex(where: { $0.publicKey == publicKey }) else { return }
        peers[index].lastSeen = date
    }
}
