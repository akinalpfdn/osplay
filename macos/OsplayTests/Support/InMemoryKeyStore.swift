import CoreBridge
import Foundation

@testable import Osplay

/// A key store without the Keychain. `@unchecked`: fields are guarded by `lock`.
final class InMemoryKeyStore: KeyStoring, @unchecked Sendable {
    private let lock = NSLock()
    private var storedIdentity: DeviceIdentity?
    private var storedPeers = PeerBook()
    var saveError: KeyStoreError?

    func identity() throws(KeyStoreError) -> DeviceIdentity {
        lock.lock()
        defer { lock.unlock() }
        if let storedIdentity { return storedIdentity }
        do throws(CoreError) {
            let created = try DeviceIdentity.generate()
            storedIdentity = created
            return created
        } catch {
            throw .identityCreationFailed(error)
        }
    }

    func peers() throws(KeyStoreError) -> PeerBook { lock.withLock { storedPeers } }

    func save(_ peers: PeerBook) throws(KeyStoreError) {
        if let saveError { throw saveError }
        lock.withLock { storedPeers = peers }
    }
}
