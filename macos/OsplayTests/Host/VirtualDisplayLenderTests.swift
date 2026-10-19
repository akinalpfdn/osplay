import CoreBridge
import Foundation
import Testing

@testable import Osplay

@MainActor
@Suite("Virtual display lender")
struct VirtualDisplayLenderTests {
    private func monitor(id: UInt8, scalePercent: Int) -> MonitorDescription {
        MonitorDescription(
            id: id, widthPixels: 2560, heightPixels: 1440, refreshMillihertz: 60_000, physicalWidthMillimeters: 598,
            physicalHeightMillimeters: 336, scalePercent: scalePercent, name: "Monitor \(id)")
    }

    @Test("A monitor at 100% starts at full size, a high-density one in crisp HiDPI")
    func initialResolutionFollowsTheViewersScaling() async throws {
        let provider = FakeVirtualDisplayProvider()
        let lender = VirtualDisplayLender(provider: provider, arrange: { _, _ in })

        _ = try await lender.lend(
            [monitor(id: 0, scalePercent: 100), monitor(id: 1, scalePercent: 200)], peerID: Data("peer".utf8),
            hiDPI: .automatic, placement: .right)

        let full = DisplaySize(width: 2560, height: 1440)
        #expect(provider.handles[0].activated.map(\.points) == [full])
        #expect(provider.handles[0].activated.map(\.pixels) == [full])
        #expect(provider.handles[1].activated.map(\.points) == [DisplaySize(width: 1280, height: 720)])
        #expect(provider.handles[1].activated.map(\.pixels) == [full])
    }

    @Test("The user's choice for a viewer overrides its scaling")
    func choiceOverridesScaling() async throws {
        let provider = FakeVirtualDisplayProvider()
        let lender = VirtualDisplayLender(provider: provider, arrange: { _, _ in })

        _ = try await lender.lend(
            [monitor(id: 0, scalePercent: 100)], peerID: Data("peer".utf8), hiDPI: .always, placement: .right)

        #expect(provider.handles[0].activated.map(\.points) == [DisplaySize(width: 1280, height: 720)])
    }
}
