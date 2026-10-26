import CoreBridge
import Foundation
import Testing

@testable import Osplay

@Suite("iPad screen as a monitor")
struct SceneScreenTests {
    @Test("The monitor takes the orientation the iPad is held in")
    func orientation() {
        let portraitPixels = CGSize(width: 1640, height: 2360)
        let landscape = SceneScreen.monitor(
            nativePixels: portraitPixels, landscape: true, scale: 2, maximumFramesPerSecond: 60, name: "iPad")
        #expect(landscape.widthPixels == 2360)
        #expect(landscape.heightPixels == 1640)
        let portrait = SceneScreen.monitor(
            nativePixels: portraitPixels, landscape: false, scale: 2, maximumFramesPerSecond: 60, name: "iPad")
        #expect(portrait.widthPixels == 1640)
        #expect(portrait.heightPixels == 2360)
    }

    @Test("Refresh rate, scale and size are what the protocol accepts")
    func values() {
        let monitor = SceneScreen.monitor(
            nativePixels: CGSize(width: 2048, height: 2732), landscape: true, scale: 2, maximumFramesPerSecond: 0,
            name: "iPad")
        #expect(monitor.refreshMillihertz == 60_000)
        #expect(monitor.scalePercent == 200)
        #expect(monitor.physicalWidthMillimeters == 263)
        #expect(monitor.name == "iPad")
    }
}

@MainActor
@Suite("iPad connection")
struct PadConnectionViewModelTests {
    private func device(minor: UInt8, version: UInt32 = 1, roles: Advertisement.Roles = .host) -> DiscoveredDevice {
        DiscoveredDevice(
            label: "pc",
            advertisement: Advertisement(
                protocolVersion: version, protocolMinor: minor, platform: .windows, roles: roles,
                deviceID: String(repeating: "ab", count: 16), name: "PC"),
            host: "pc.local.", port: 4000)
    }

    private func model() throws -> PadConnectionViewModel {
        let defaults = try #require(UserDefaults(suiteName: "PadConnectionViewModelTests"))
        defaults.removePersistentDomain(forName: "PadConnectionViewModelTests")
        return PadConnectionViewModel(
            dependencies: PadDependencies(
                keyStore: InMemoryKeyStore(), discovery: SilentDiscovery(), settings: PadSettings(defaults: defaults)))
    }

    @Test("A computer with Osplay 0.1 needs an update before it can show on an iPad")
    func oldHost() throws {
        let connection = try model()
        #expect(connection.state(of: device(minor: 0)) == .needsUpdate)
        #expect(connection.state(of: device(minor: 2)) == .new)
        #expect(connection.state(of: device(minor: 2, version: 2)) == .incompatible)
    }

    @Test("Connecting to an old computer explains the update instead of trying")
    func connectToOldHost() throws {
        let connection = try model()
        connection.connect(to: device(minor: 0))
        #expect(connection.failure == .needsUpdate("PC"))
        #expect(connection.status == .idle)
    }

    @Test("The device name the computer sees is never empty and fits the protocol")
    func deviceName() throws {
        let defaults = try #require(UserDefaults(suiteName: "PadSettingsTests"))
        defaults.removePersistentDomain(forName: "PadSettingsTests")
        let settings = PadSettings(defaults: defaults)
        settings.deviceName = "   "
        #expect(!settings.protocolDeviceName.isEmpty)
        settings.deviceName = String(repeating: "ş", count: 40)
        #expect(settings.protocolDeviceName.utf8.count <= 64)
    }

    @Test("Every failure has text")
    func messages() {
        let failures: [PadFailure] = [
            .keyStore(.damaged), .discovery(.localNetworkDenied), .session(.peerBusy), .incompatible,
            .needsUpdate("PC"), .noScreen,
        ]
        for failure in failures {
            #expect(!failure.message.isEmpty)
        }
    }
}

/// Discovery that finds nothing.
private struct SilentDiscovery: PeerDiscovering {
    func advertise(_ advertisement: Advertisement, port: UInt16) throws(DiscoveryError) -> any Advertising {
        throw .service(0)
    }

    func browse() -> AsyncStream<DiscoveryEvent> {
        AsyncStream { _ in }
    }
}
