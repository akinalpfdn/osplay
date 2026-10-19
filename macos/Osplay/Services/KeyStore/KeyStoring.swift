import CoreBridge
import Foundation

enum KeyStoreError: Error, Equatable, Sendable {
    /// The Keychain refused the operation (OSStatus).
    case keychain(Int32)
    /// A stored item exists but cannot be read back.
    case damaged
    case identityCreationFailed(CoreError)
}

/// Where the device identity and the paired devices live (DEVPLAN: Keychain on macOS).
protocol KeyStoring: Sendable {
    /// The identity, created and stored on first use.
    func identity() throws(KeyStoreError) -> DeviceIdentity
    func peers() throws(KeyStoreError) -> PeerBook
    func save(_ peers: PeerBook) throws(KeyStoreError)
}
