import OsplayCore
import Security

/// This device's static key pair (docs/SECURITY_MODEL.md). The app stores `secretKey` in the
/// Keychain; core never persists it.
public struct DeviceIdentity: Sendable {
    public let secretKey: [UInt8]
    public let publicKey: [UInt8]

    /// A new identity from the system's cryptographically secure random generator.
    public static func generate() throws(CoreError) -> DeviceIdentity {
        var source = osplay_random_source(fill: systemRandom, context: nil)
        var secret = [UInt8](repeating: 0, count: Int(OSPLAY_KEY_SIZE))
        var publicKey = [UInt8](repeating: 0, count: Int(OSPLAY_KEY_SIZE))
        let result = osplay_identity_generate(&source, &secret, &publicKey)
        if let error = CoreError(result: result) {
            throw error
        }
        return DeviceIdentity(secretKey: secret, publicKey: publicKey)
    }

    /// Restores an identity from a stored secret key; rejects malformed or all-zero secrets.
    public init(secretKey: [UInt8]) throws(CoreError) {
        guard secretKey.count == Int(OSPLAY_KEY_SIZE) else { throw .invalidArgument }
        var publicKey = [UInt8](repeating: 0, count: Int(OSPLAY_KEY_SIZE))
        let result = osplay_identity_public_key(secretKey, &publicKey)
        if let error = CoreError(result: result) {
            throw error
        }
        self.init(secretKey: secretKey, publicKey: publicKey)
    }

    private init(secretKey: [UInt8], publicKey: [UInt8]) {
        self.secretKey = secretKey
        self.publicKey = publicKey
    }

    /// The ID advertised over Bonjour: lowercase hex of the device ID bytes.
    public var deviceID: String {
        var id = [UInt8](repeating: 0, count: Int(OSPLAY_DEVICE_ID_SIZE))
        // Cannot fail: both buffers have the documented sizes.
        _ = osplay_identity_device_id(publicKey, &id)
        return id.map { byte in
            let hex = String(byte, radix: 16)
            return byte < 0x10 ? "0" + hex : hex
        }.joined()
    }
}

/// `osplay_random_source.fill` backed by SecRandomCopyBytes.
private func systemRandom(
    _ context: UnsafeMutableRawPointer?, _ buffer: UnsafeMutablePointer<UInt8>?, _ length: Int
) -> osplay_result {
    guard let buffer else { return OSPLAY_ERROR_INVALID_ARGUMENT }
    return SecRandomCopyBytes(kSecRandomDefault, length, buffer) == errSecSuccess
        ? OSPLAY_OK : OSPLAY_ERROR_RANDOM_UNAVAILABLE
}
