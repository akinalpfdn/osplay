import CoreBridge
import Foundation

/// Why the iPad cannot list, connect to or keep showing a computer.
enum PadFailure: Error, Equatable, Sendable {
    case keyStore(KeyStoreError)
    case discovery(DiscoveryError)
    case session(CoreError)
    /// The computer runs a different major protocol version.
    case incompatible
    /// The computer runs Osplay 0.1, which refuses iPads.
    case needsUpdate(String)
    /// The screen size could not be read.
    case noScreen
}

extension PadFailure {
    var message: String {
        switch self {
        case .keyStore(let error):
            switch error {
            case .keychain(let status): String(localized: "pad.error.keyStore.keychain \(Int(status))")
            case .damaged: String(localized: "pad.error.keyStore.damaged")
            case .identityCreationFailed: String(localized: "pad.error.keyStore.identity")
            }
        case .discovery(let error):
            switch error {
            case .localNetworkDenied: String(localized: "pad.error.localNetwork")
            case .service(let code): String(localized: "pad.error.discovery \(Int(code))")
            case .invalidAdvertisement: String(localized: "pad.error.discovery \(0)")
            }
        case .session(let error):
            error.padMessage
        case .incompatible:
            String(localized: "pad.error.incompatible")
        case .needsUpdate(let name):
            String(localized: "pad.error.needsUpdate \(name)")
        case .noScreen:
            String(localized: "pad.error.noScreen")
        }
    }

    var needsLocalNetworkPermission: Bool {
        self == .discovery(.localNetworkDenied)
    }
}

extension CoreError {
    /// Text for a session that ended with this error: what happened and what to do.
    var padMessage: String {
        switch self {
        case .notTrusted: String(localized: "pad.error.session.notTrusted")
        case .pairingRejected: String(localized: "pad.error.session.pairingRejected")
        case .unsupportedVersion: String(localized: "pad.error.incompatible")
        case .peerBusy: String(localized: "pad.error.session.peerBusy")
        case .timedOut, .network, .connectionClosed: String(localized: "pad.error.session.unreachable")
        case .peerSleeping: String(localized: "pad.error.session.peerSleeping")
        case .invalidArgument, .bufferTooSmall, .truncated, .badMagic, .unknownType, .trailingBytes,
            .valueOutOfRange, .invalidString, .tooLarge, .unexpectedMessage, .handshakeOutOfOrder:
            String(localized: "pad.error.session.protocol")
        case .decryptionFailed, .replayed, .nonceExhausted, .invalidKey:
            String(localized: "pad.error.session.encryption")
        case .randomUnavailable: String(localized: "pad.error.session.random")
        case .peerError: String(localized: "pad.error.session.peerError")
        case .invalidState: String(localized: "pad.error.session.busy")
        case .internalFailure, .unknown: String(localized: "pad.error.session.internal \(Int(code))")
        }
    }
}
