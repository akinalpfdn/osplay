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
    case timedOut
    case connectionClosed
    case peerError
    case unexpectedMessage
    case notTrusted
    case pairingRejected
    case peerBusy
    case invalidState
    case network
    case internalFailure
    /// The peer closed because it is going to sleep; it is expected back.
    case peerSleeping
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
        case OSPLAY_ERROR_TIMED_OUT: self = .timedOut
        case OSPLAY_ERROR_CONNECTION_CLOSED: self = .connectionClosed
        case OSPLAY_ERROR_PEER_ERROR: self = .peerError
        case OSPLAY_ERROR_UNEXPECTED_MESSAGE: self = .unexpectedMessage
        case OSPLAY_ERROR_NOT_TRUSTED: self = .notTrusted
        case OSPLAY_ERROR_PAIRING_REJECTED: self = .pairingRejected
        case OSPLAY_ERROR_PEER_BUSY: self = .peerBusy
        case OSPLAY_ERROR_INVALID_STATE: self = .invalidState
        case OSPLAY_ERROR_NETWORK: self = .network
        case OSPLAY_ERROR_INTERNAL: self = .internalFailure
        case OSPLAY_ERROR_PEER_SLEEPING: self = .peerSleeping
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
        case .timedOut: OSPLAY_ERROR_TIMED_OUT.rawValue
        case .connectionClosed: OSPLAY_ERROR_CONNECTION_CLOSED.rawValue
        case .peerError: OSPLAY_ERROR_PEER_ERROR.rawValue
        case .unexpectedMessage: OSPLAY_ERROR_UNEXPECTED_MESSAGE.rawValue
        case .notTrusted: OSPLAY_ERROR_NOT_TRUSTED.rawValue
        case .pairingRejected: OSPLAY_ERROR_PAIRING_REJECTED.rawValue
        case .peerBusy: OSPLAY_ERROR_PEER_BUSY.rawValue
        case .invalidState: OSPLAY_ERROR_INVALID_STATE.rawValue
        case .network: OSPLAY_ERROR_NETWORK.rawValue
        case .internalFailure: OSPLAY_ERROR_INTERNAL.rawValue
        case .peerSleeping: OSPLAY_ERROR_PEER_SLEEPING.rawValue
        case .unknown(let code): code
        }
    }
}
