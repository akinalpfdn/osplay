import OsplayCore
import Security

/// The platform's cryptographically secure random generator, as core expects it.
enum SystemRandom {
    static var source: osplay_random_source { osplay_random_source(fill: fill, context: nil) }
}

/// `osplay_random_source.fill` backed by SecRandomCopyBytes. Thread-safe.
private func fill(
    _ context: UnsafeMutableRawPointer?, _ buffer: UnsafeMutablePointer<UInt8>?, _ length: Int
) -> osplay_result {
    guard let buffer else { return OSPLAY_ERROR_INVALID_ARGUMENT }
    return SecRandomCopyBytes(kSecRandomDefault, length, buffer) == errSecSuccess
        ? OSPLAY_OK : OSPLAY_ERROR_RANDOM_UNAVAILABLE
}
