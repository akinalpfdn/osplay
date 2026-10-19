import CoreBridge
import Foundation
import Testing

@testable import Osplay

/// Runs against this Mac's mDNSResponder and Keychain (DEVPLAN Phase 10). The test host (the
/// debug Osplay app) needs Local Network access: allow it when macOS asks.
@Suite("Bonjour and Keychain on this Mac", .serialized)
struct DiscoveryHardwareTests {
    private static let timeout: Duration = .seconds(20)

    @Test("An advertised host is found with its TXT record and lost when withdrawn")
    func advertiseAndBrowse() async throws {
        let identity = try DeviceIdentity.generate()
        let discovery = DNSServiceDiscovery()
        let advertisement = Advertisement(identity: identity, name: "Osplay Hardware Test", roles: .host)
        let events = discovery.browse()
        let registration = try discovery.advertise(advertisement, port: 50_123)

        let device = try #require(
            await Self.first(in: events) { event in
                if case .found(let device) = event, device.advertisement.deviceID == identity.deviceID {
                    return device
                }
                return nil
            })
        #expect(device.advertisement == advertisement)
        #expect(device.port == 50_123)
        #expect(device.host.hasSuffix(".local."))

        registration.cancel()
        let lost = await Self.first(in: events) { event in
            if case .lost(let label) = event, label == device.label { return true }
            return nil
        }
        #expect(lost == true)
    }

    @Test("The Keychain keeps the identity and the paired devices")
    func keychainRoundTrip() throws {
        let service = "com.akinalpfdn.osplay.hardware-test"
        let store = KeychainKeyStore(service: service)
        defer {
            SecItemDelete([kSecClass: kSecClassGenericPassword, kSecAttrService: service] as CFDictionary)
        }
        let first = try store.identity()
        #expect(try store.identity().publicKey == first.publicKey)

        var book = PeerBook()
        book.trust([UInt8](repeating: 3, count: 32), name: "PC", at: Date())
        try store.save(book)
        #expect(try store.peers() == book)
    }

    /// The first value `match` returns, or nil after the timeout or a browse failure (recorded).
    private static func first<Value: Sendable>(
        in events: AsyncStream<DiscoveryEvent>, match: @escaping @Sendable (DiscoveryEvent) -> Value?
    ) async -> Value? {
        await withTaskGroup(of: Value?.self) { group in
            group.addTask {
                for await event in events {
                    if case .failed(let reason) = event {
                        Issue.record("browse failed: \(reason)")
                        return nil
                    }
                    if let value = match(event) { return value }
                }
                return nil
            }
            group.addTask {
                // Cancellation means the other task finished first.
                try? await Task.sleep(for: timeout)
                return nil
            }
            let result = await group.next() ?? nil
            group.cancelAll()
            return result
        }
    }
}
