import CoreBridge
import Foundation

/// Why connecting, sharing or viewing stopped.
enum ConnectionError: Error, Equatable, Sendable {
    case keyStore(KeyStoreError)
    case discovery(DiscoveryError)
    case host(HostSessionError)
    case session(CoreError)
    /// No display to offer as a viewer.
    case noDisplay
    /// The other device speaks a different protocol version.
    case incompatible
}

extension ConnectionError {
    var message: String {
        switch self {
        case .keyStore(let error):
            switch error {
            case .keychain(let status): String(localized: "error.keyStore.keychain \(Int(status))")
            case .damaged: String(localized: "error.keyStore.damaged")
            case .identityCreationFailed: String(localized: "error.keyStore.identity")
            }
        case .discovery(let error):
            switch error {
            case .invalidAdvertisement: String(localized: "error.discovery.name")
            case .service(let code): String(localized: "error.discovery.service \(Int(code))")
            case .localNetworkDenied: String(localized: "error.discovery.localNetwork")
            }
        case .host(let error):
            error.message
        case .session(let error):
            error.sessionMessage
        case .noDisplay:
            String(localized: "error.connection.noDisplay")
        case .incompatible:
            String(localized: "error.connection.incompatible")
        }
    }

    var needsScreenRecordingPermission: Bool {
        if case .host(let error) = self { return error.needsScreenRecordingPermission }
        return false
    }

    var needsLocalNetworkPermission: Bool {
        self == .discovery(.localNetworkDenied)
    }
}

extension CoreError {
    /// Text for a session that ended with this error: what happened and what to do.
    var sessionMessage: String {
        switch self {
        case .notTrusted: String(localized: "error.session.notTrusted")
        case .pairingRejected: String(localized: "error.session.pairingRejected")
        case .unsupportedVersion: String(localized: "error.connection.incompatible")
        case .peerBusy: String(localized: "error.session.peerBusy")
        case .timedOut, .network, .connectionClosed: String(localized: "error.session.unreachable")
        case .invalidArgument, .bufferTooSmall, .truncated, .badMagic, .unknownType, .trailingBytes,
            .valueOutOfRange, .invalidString, .tooLarge, .unexpectedMessage, .handshakeOutOfOrder:
            String(localized: "error.session.protocol")
        case .decryptionFailed, .replayed, .nonceExhausted, .invalidKey:
            String(localized: "error.session.encryption")
        case .randomUnavailable: String(localized: "error.session.random")
        case .peerError: String(localized: "error.session.peerError")
        case .invalidState: String(localized: "error.session.busy")
        case .internalFailure, .unknown: String(localized: "error.session.internal \(Int(code))")
        }
    }
}
