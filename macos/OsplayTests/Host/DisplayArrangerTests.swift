import CoreGraphics
import Testing

@testable import Osplay

@Suite("Display arrangement")
struct DisplayArrangerTests {
    private let main = CGRect(x: 0, y: 0, width: 1710, height: 1107)
    private let sizes = [CGSize(width: 2560, height: 1440), CGSize(width: 1920, height: 1080)]

    @Test("Right: a row starting at the main display's right edge, tops aligned")
    func right() {
        let origins = DisplayArranger.origins(for: sizes, beside: main, placement: .right)
        #expect(origins == [CGPoint(x: 1710, y: 0), CGPoint(x: 4270, y: 0)])
    }

    @Test("Left: the row ends at the main display's left edge, in the viewer's order")
    func left() {
        let origins = DisplayArranger.origins(for: sizes, beside: main, placement: .left)
        #expect(origins == [CGPoint(x: -4480, y: 0), CGPoint(x: -1920, y: 0)])
    }

    @Test("Above and below: the row is centred on the main display")
    func aboveAndBelow() {
        let above = DisplayArranger.origins(for: sizes, beside: main, placement: .above)
        #expect(above == [CGPoint(x: -1385, y: -1440), CGPoint(x: 1175, y: -1080)])
        let below = DisplayArranger.origins(for: sizes, beside: main, placement: .below)
        #expect(below.map(\.y) == [1107, 1107])
    }
}
