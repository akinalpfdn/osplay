import CoreBridge
import Foundation

/// An Osplay device on the network.
struct DiscoveredDevice: Sendable, Equatable, Identifiable {
    /// The service instance name; identifies the device in `.lost`.
    let label: String
    let advertisement: Advertisement
    /// Host name (e.g. `DESKTOP-1.local.`) the core resolves when connecting.
    let host: String
    let port: UInt16

    var id: String { label }
}

enum DiscoveryEvent: Sendable, Equatable {
    case found(DiscoveredDevice)
    case lost(label: String)
    /// Browsing stopped; the stream ends after this.
    case failed(DiscoveryError)
}

enum DiscoveryError: Error, Equatable, Sendable {
    case invalidAdvertisement(CoreError)
    /// mDNSResponder refused (DNSServiceErrorType).
    case service(Int32)
    /// macOS Local Network privacy blocks this app.
    case localNetworkDenied
}

/// A running Bonjour registration; cancelling withdraws it.
protocol Advertising: AnyObject, Sendable {
    func cancel()
}

/// Bonjour advertise and browse (DEVPLAN: `PeerDiscovering`).
protocol PeerDiscovering: Sendable {
    /// Announces this device's control `port`; the service runs until cancelled.
    func advertise(_ advertisement: Advertisement, port: UInt16) throws(DiscoveryError) -> any Advertising
    /// Every Osplay device seen, for as long as the stream is consumed.
    func browse() -> AsyncStream<DiscoveryEvent>
}
