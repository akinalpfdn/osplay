import CoreGraphics
import Foundation
import Testing

@testable import Osplay

/// Runs against the real window server: displays really appear while these tests run.
/// Kept out of the default scheme; run with the OsplayHardware scheme (docs/BUILDING.md).
@MainActor
@Suite("CGVirtualDisplay on this Mac", .serialized)
struct CGVirtualDisplayProviderHardwareTests {
    private static let peerID = Data("osplay-hardware-test-peer".utf8)

    private func configuration(index: Int, isHiDPI: Bool = true) throws -> VirtualDisplayConfiguration {
        try VirtualDisplayConfiguration(
            name: "Osplay Hardware Test \(index + 1)",
            identity: .derived(peerID: Self.peerID, monitorIndex: index),
            pixelSize: DisplaySize(width: 2560, height: 1440),
            refreshRate: 60,
            physicalSizeMillimeters: CGSize(width: 597, height: 336),
            isHiDPI: isHiDPI
        )
    }

    private static func onlineDisplays() -> Set<CGDirectDisplayID> {
        var ids = [CGDirectDisplayID](repeating: 0, count: 32)
        var count: UInt32 = 0
        guard CGGetOnlineDisplayList(UInt32(ids.count), &ids, &count) == .success else { return [] }
        return Set(ids.prefix(Int(count)))
    }

    /// The window server applies changes asynchronously.
    private static func waitUntil(_ condition: () -> Bool) async throws -> Bool {
        for _ in 0..<40 where !condition() {
            try await Task.sleep(for: .milliseconds(50))
        }
        return condition()
    }

    @Test("Two displays exist at the same time and disappear when destroyed")
    func twoSimultaneousDisplays() async throws {
        let provider = CGVirtualDisplayProvider()
        let first = try provider.createDisplay(configuration(index: 0))
        let second = try provider.createDisplay(configuration(index: 1))
        let ids: Set = [first.displayID, second.displayID]

        #expect(try await Self.waitUntil { ids.isSubset(of: Self.onlineDisplays()) })

        provider.destroyAll()

        #expect(provider.displays.isEmpty)
        #expect(try await Self.waitUntil { ids.isDisjoint(with: Self.onlineDisplays()) })
    }

    @Test("The crisp HiDPI resolution is offered and can be activated")
    func activatesCrispHiDPI() async throws {
        let provider = CGVirtualDisplayProvider()
        defer { provider.destroyAll() }
        let handle = try provider.createDisplay(configuration(index: 0))
        let crisp = try #require(handle.configuration.crispHiDPIResolution)

        #expect(try await Self.waitUntil { handle.availableResolutions().contains(crisp) })
        try handle.activate(crisp)

        #expect(try await Self.waitUntil { handle.currentResolution()?.pixels == crisp.pixels })
        #expect(handle.currentResolution()?.points == crisp.points)
    }

    @Test("A recreated display with the same identity returns to the same position")
    func identityRestoresArrangement() async throws {
        let provider = CGVirtualDisplayProvider()
        let handle = try provider.createDisplay(configuration(index: 0))
        #expect(try await Self.waitUntil { Self.onlineDisplays().contains(handle.displayID) })
        let origin = CGDisplayBounds(handle.displayID).origin
        let oldID = handle.displayID
        provider.destroyAll()
        #expect(try await Self.waitUntil { !Self.onlineDisplays().contains(oldID) })

        let recreated = try provider.createDisplay(configuration(index: 0))
        defer { provider.destroyAll() }
        #expect(try await Self.waitUntil { Self.onlineDisplays().contains(recreated.displayID) })

        #expect(CGDisplayBounds(recreated.displayID).origin == origin)
    }

    @Test("Duplicate identities and a third display are refused")
    func enforcesLimits() throws {
        let provider = CGVirtualDisplayProvider()
        defer { provider.destroyAll() }
        _ = try provider.createDisplay(configuration(index: 0))

        #expect(throws: VirtualDisplayError.duplicateIdentity) {
            try provider.createDisplay(configuration(index: 0))
        }
        _ = try provider.createDisplay(configuration(index: 1))
        #expect(throws: VirtualDisplayError.tooManyDisplays) {
            try provider.createDisplay(configuration(index: 2))
        }
    }

    @Test("Updating the configuration keeps the display ID")
    func updateKeepsDisplayID() async throws {
        let provider = CGVirtualDisplayProvider()
        defer { provider.destroyAll() }
        let handle = try provider.createDisplay(configuration(index: 0))
        let id = handle.displayID

        try handle.updateConfiguration(configuration(index: 0, isHiDPI: false))

        #expect(handle.displayID == id)
        #expect(try await Self.waitUntil { Self.onlineDisplays().contains(id) })
    }
}
