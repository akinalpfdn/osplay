import CoreBridge
import Foundation
import Testing

@testable import Osplay

@Suite("Paired devices")
struct PeerBookTests {
    private let mac = [UInt8](repeating: 1, count: 32)
    private let pc = [UInt8](repeating: 2, count: 32)

    @Test("An unknown device pairs; a paired one connects without a code")
    func trustAfterPairing() {
        var book = PeerBook()
        #expect(book.trust(pc) == .unknown)
        book.trust(pc, name: "Akinalp's PC", at: Date())
        #expect(book.trust(pc) == .trusted)
        #expect(book.trust(mac) == .unknown)
    }

    @Test("A revoked device is refused until it pairs again on purpose")
    func revoke() {
        var book = PeerBook()
        book.trust(pc, name: "PC", at: Date())
        book.revoke(pc)
        #expect(book.trust(pc) == .blocked)
        book.trust(pc, name: "PC", at: Date())
        #expect(book.trust(pc) == .trusted)
        book.remove(pc)
        #expect(book.trust(pc) == .unknown)
    }

    @Test("The cache answers trust from memory and writes changes through")
    func cache() throws {
        let store = InMemoryKeyStore()
        let cache = PeerTrustCache(store: store)
        try cache.load()
        let peer = SessionPeer()
        #expect(peer.trust(pc, in: cache) == .unknown)
        #expect(peer.key == pc, "the session knows whose key it asked about")

        try cache.paired(pc, name: "PC")
        #expect(cache.trust(pc) == .trusted)
        #expect(try store.peers().trust(pc) == .trusted)

        try cache.revoke(pc)
        #expect(try store.peers().trust(pc) == .blocked)
    }

    @Test("Paired devices survive encoding")
    func codable() throws {
        var book = PeerBook()
        book.trust(pc, name: "Çalışma PC", at: Date(timeIntervalSince1970: 1_760_000_000))
        let decoded = try JSONDecoder().decode(PeerBook.self, from: JSONEncoder().encode(book))
        #expect(decoded == book)
    }
}
