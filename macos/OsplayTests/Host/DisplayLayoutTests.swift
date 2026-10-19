import CoreGraphics
import Testing

@testable import Osplay

@Suite("Display layout")
struct DisplayLayoutTests {
    private let main = CGRect(x: 0, y: 0, width: 1710, height: 1107)

    @Test("A display dropped on top of the main one moves out to the nearest edge")
    func overlapIsResolved() {
        let dropped = CGRect(x: 1500, y: 100, width: 2560, height: 1440)
        let result = DisplayLayout.snap(dropped, among: [main])
        #expect(!result.intersects(main.insetBy(dx: 0.5, dy: 0.5)))
        #expect(result.minX == main.maxX)
    }

    @Test("A display dropped far away comes back to touch its neighbour")
    func gapIsClosed() {
        let dropped = CGRect(x: -3000, y: -2000, width: 1920, height: 1080)
        let result = DisplayLayout.snap(dropped, among: [main])
        let touches = result.maxX == main.minX || result.maxY == main.minY
        #expect(touches)
    }

    @Test("Nearly level tops snap level")
    func edgesAlign() {
        let dropped = CGRect(x: 1800, y: 15, width: 1920, height: 1080)
        let result = DisplayLayout.snap(dropped, among: [main])
        #expect(result.origin == CGPoint(x: 1710, y: 0))
    }

    @Test("A saved arrangement is reused only for the same monitors at the same sizes")
    func savedArrangement() {
        let monitors: [(id: UInt8, name: String, size: CGSize)] = [
            (0, "Acer", CGSize(width: 2560, height: 1440)), (1, "VZ239", CGSize(width: 1920, height: 1080)),
        ]
        let saved = [
            ArrangedMonitor(
                id: 0, name: "Acer", size: CGSize(width: 2560, height: 1440), origin: CGPoint(x: -2560, y: 0)),
            ArrangedMonitor(
                id: 1, name: "VZ239", size: CGSize(width: 1920, height: 1080), origin: CGPoint(x: 0, y: -1080)),
        ]
        let reused = DisplayLayout.arrangement(for: monitors, saved: saved, main: main.size)
        #expect(reused.map(\.origin) == [CGPoint(x: -2560, y: 0), CGPoint(x: 0, y: -1080)])

        var resized = monitors
        resized[1].size = CGSize(width: 1280, height: 720)
        let fresh = DisplayLayout.arrangement(for: resized, saved: saved, main: main.size)
        #expect(fresh.map(\.origin) == [CGPoint(x: 1710, y: 0), CGPoint(x: 4270, y: 0)])
    }
}
