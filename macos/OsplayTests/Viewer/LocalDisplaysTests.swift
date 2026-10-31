import CoreGraphics
import Foundation
import Testing

@testable import Osplay

@Suite("Local displays")
struct LocalDisplaysTests {
    private func candidate(
        _ id: CGDirectDisplayID, _ name: String, x: CGFloat, y: CGFloat = 0, width: Int = 2560, height: Int = 1440,
        refresh: Double = 60, scale: CGFloat = 2
    ) -> LocalDisplays.Candidate {
        LocalDisplays.Candidate(
            displayID: id, name: name,
            frame: CGRect(x: x, y: y, width: CGFloat(width) / scale, height: CGFloat(height) / scale),
            widthPixels: width, heightPixels: height, refreshRate: refresh,
            physicalMillimeters: CGSize(width: 597, height: 336), backingScale: scale)
    }

    @Test("IDs follow the arrangement from left to right, then top to bottom")
    func ordersByPlace() {
        let displays = LocalDisplays.offered(from: [
            candidate(7, "Right", x: 1280),
            candidate(5, "Built-in", x: 0),
            candidate(9, "Above", x: 0, y: -900),
            candidate(3, "Left", x: -1920),
        ])
        #expect(displays.map(\.description.name) == ["Left", "Above", "Built-in", "Right"])
        #expect(displays.map(\.id) == [0, 1, 2, 3])
        #expect(displays.map(\.displayID) == [3, 9, 5, 7])
        #expect(displays[2].key == "Built-in|0|0")
    }

    @Test("At most four displays, and none the protocol cannot describe")
    func limits() {
        let displays = LocalDisplays.offered(from: [
            candidate(1, "A", x: 0), candidate(2, "B", x: 100), candidate(3, "C", x: 200),
            candidate(4, "Tiny", x: 250, width: 300, height: 200), candidate(5, "D", x: 300), candidate(6, "E", x: 400),
        ])
        #expect(displays.map(\.description.name) == ["A", "B", "C", "D"])
    }

    @Test("A panel without a refresh rate is offered at 60 Hz; the scale is in percent")
    func describesMode() throws {
        let display = try #require(LocalDisplays.offered(from: [candidate(1, "Built-in", x: 0, refresh: 0)]).first)
        #expect(display.description.refreshMillihertz == 60_000)
        #expect(display.description.scalePercent == 200)
        #expect(display.description.widthPixels == 2560)
    }

    @Test("Names fit the protocol: at most 64 bytes, cut between characters, no control characters")
    func protocolNames() {
        let long = String(repeating: "ş", count: 40)  // 80 bytes
        let name = ProtocolText.name(long)
        #expect(name.utf8.count <= 64)
        #expect(name == String(repeating: "ş", count: 32))
        #expect(ProtocolText.name("LG\u{7}HDR\n4K") == "LGHDR4K")
    }

    @Test("Only size, refresh rate and scale count as a new mode")
    func sameMode() throws {
        let before = try #require(LocalDisplays.offered(from: [candidate(1, "A", x: 0)]).first)
        let moved = try #require(
            LocalDisplays.offered(from: [candidate(1, "A", x: 0), candidate(2, "B", x: -2000)]).last)
        let resized = try #require(
            LocalDisplays.offered(from: [candidate(1, "A", x: 0, width: 1920, height: 1080)]).first)
        #expect(moved.id != before.id)
        #expect(moved.hasSameMode(as: before), "a new ID or place is not a new mode")
        #expect(!resized.hasSameMode(as: before))
    }

    @Test("The picker keeps the arrangement: side by side, scaled to fit, centered, with a gap")
    func pickerGeometry() throws {
        let displays = LocalDisplays.offered(from: [
            candidate(1, "Left", x: 0, width: 2000, height: 1000, scale: 1),
            candidate(2, "Right", x: 2000, width: 2000, height: 1000, scale: 1),
        ])
        let frames = DisplayPickerGeometry.tileFrames(of: displays, in: CGSize(width: 400, height: 300), gap: 6)
        let left = try #require(frames[0])
        let right = try #require(frames[1])
        // 4000×1000 points into 400×300: scale 0.1, 100 points of space above and below.
        #expect(left == CGRect(x: 3, y: 103, width: 194, height: 94))
        #expect(right == CGRect(x: 203, y: 103, width: 194, height: 94))
        #expect(DisplayPickerGeometry.tileFrames(of: [], in: CGSize(width: 400, height: 300), gap: 6).isEmpty)
    }
}
