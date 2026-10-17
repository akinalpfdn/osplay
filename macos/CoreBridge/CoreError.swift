import OsplayCore

/// A failure reported by the shared core (`osplay_result`). The UI maps cases to localized text.
public enum CoreError: Error, Equatable, Sendable {
    case invalidArgument
    case bufferTooSmall
    case truncated
    case badMagic
    case unsupportedVersion
    case unknownType
    case trailingBytes
    case valueOutOfRange
    case invalidString
    case tooLarge
    case decryptionFailed
    case handshakeOutOfOrder
    case replayed
    case nonceExhausted
    case invalidKey
    case randomUnavailable
    /// A code added to the core after this app was built.
    case unknown(code: UInt32)

    /// `nil` for `OSPLAY_OK`.
    public init?(result: osplay_result) {
        switch result {
        case OSPLAY_OK: return nil
        case OSPLAY_ERROR_INVALID_ARGUMENT: self = .invalidArgument
        case OSPLAY_ERROR_BUFFER_TOO_SMALL: self = .bufferTooSmall
        case OSPLAY_ERROR_TRUNCATED: self = .truncated
        case OSPLAY_ERROR_BAD_MAGIC: self = .badMagic
        case OSPLAY_ERROR_UNSUPPORTED_VERSION: self = .unsupportedVersion
        case OSPLAY_ERROR_UNKNOWN_TYPE: self = .unknownType
        case OSPLAY_ERROR_TRAILING_BYTES: self = .trailingBytes
        case OSPLAY_ERROR_VALUE_OUT_OF_RANGE: self = .valueOutOfRange
        case OSPLAY_ERROR_INVALID_STRING: self = .invalidString
        case OSPLAY_ERROR_TOO_LARGE: self = .tooLarge
        case OSPLAY_ERROR_DECRYPTION_FAILED: self = .decryptionFailed
        case OSPLAY_ERROR_HANDSHAKE_OUT_OF_ORDER: self = .handshakeOutOfOrder
        case OSPLAY_ERROR_REPLAYED: self = .replayed
        case OSPLAY_ERROR_NONCE_EXHAUSTED: self = .nonceExhausted
        case OSPLAY_ERROR_INVALID_KEY: self = .invalidKey
        case OSPLAY_ERROR_RANDOM_UNAVAILABLE: self = .randomUnavailable
        default: self = .unknown(code: result.rawValue)
        }
    }

    /// The numeric `osplay_result` value.
    public var code: UInt32 {
        switch self {
        case .invalidArgument: OSPLAY_ERROR_INVALID_ARGUMENT.rawValue
        case .bufferTooSmall: OSPLAY_ERROR_BUFFER_TOO_SMALL.rawValue
        case .truncated: OSPLAY_ERROR_TRUNCATED.rawValue
        case .badMagic: OSPLAY_ERROR_BAD_MAGIC.rawValue
        case .unsupportedVersion: OSPLAY_ERROR_UNSUPPORTED_VERSION.rawValue
        case .unknownType: OSPLAY_ERROR_UNKNOWN_TYPE.rawValue
        case .trailingBytes: OSPLAY_ERROR_TRAILING_BYTES.rawValue
        case .valueOutOfRange: OSPLAY_ERROR_VALUE_OUT_OF_RANGE.rawValue
        case .invalidString: OSPLAY_ERROR_INVALID_STRING.rawValue
        case .tooLarge: OSPLAY_ERROR_TOO_LARGE.rawValue
        case .decryptionFailed: OSPLAY_ERROR_DECRYPTION_FAILED.rawValue
        case .handshakeOutOfOrder: OSPLAY_ERROR_HANDSHAKE_OUT_OF_ORDER.rawValue
        case .replayed: OSPLAY_ERROR_REPLAYED.rawValue
        case .nonceExhausted: OSPLAY_ERROR_NONCE_EXHAUSTED.rawValue
        case .invalidKey: OSPLAY_ERROR_INVALID_KEY.rawValue
        case .randomUnavailable: OSPLAY_ERROR_RANDOM_UNAVAILABLE.rawValue
        case .unknown(let code): code
        }
    }
}
