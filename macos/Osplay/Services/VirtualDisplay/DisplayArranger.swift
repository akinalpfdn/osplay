import CoreGraphics

/// Where a viewer's displays sit next to this Mac's main display.
enum DisplayPlacement: String, CaseIterable, Codable, Sendable, Identifiable {
    case right
    case left
    case above
    case below

    var id: Self { self }
}

/// Places a viewer's displays in one row beside the main display, in the viewer's monitor order
/// (left to right on the viewer), the way System Settings › Displays › Arrange would.
enum DisplayArranger {
    /// Desktop origins for displays of these point sizes, against the main display's bounds.
    static func origins(for sizes: [CGSize], beside main: CGRect, placement: DisplayPlacement) -> [CGPoint] {
        let totalWidth = sizes.reduce(0) { $0 + $1.width }
        let startX: CGFloat
        let y: (CGSize) -> CGFloat
        switch placement {
        case .right:
            startX = main.maxX
            y = { _ in main.minY }
        case .left:
            startX = main.minX - totalWidth
            y = { _ in main.minY }
        case .above:
            startX = (main.midX - totalWidth / 2).rounded()
            y = { size in main.minY - size.height }
        case .below:
            startX = (main.midX - totalWidth / 2).rounded()
            y = { _ in main.maxY }
        }
        var x = startX
        return sizes.map { size in
            defer { x += size.width }
            return CGPoint(x: x, y: y(size))
        }
    }

    /// Moves the displays there; macOS keeps the arrangement for their identities.
    @MainActor
    static func arrange(
        _ displays: [(id: CGDirectDisplayID, size: CGSize)], placement: DisplayPlacement
    )
        throws(VirtualDisplayError)
    {
        guard !displays.isEmpty else { return }
        let origins = origins(
            for: displays.map(\.size), beside: CGDisplayBounds(CGMainDisplayID()), placement: placement)
        var config: CGDisplayConfigRef?
        let begin = CGBeginDisplayConfiguration(&config)
        guard begin == .success, let config else { throw .reconfigurationFailed(begin.rawValue) }
        for (display, origin) in zip(displays, origins) {
            let result = CGConfigureDisplayOrigin(config, display.id, Int32(origin.x), Int32(origin.y))
            guard result == .success else {
                CGCancelDisplayConfiguration(config)
                throw .reconfigurationFailed(result.rawValue)
            }
        }
        let complete = CGCompleteDisplayConfiguration(config, .permanently)
        guard complete == .success else { throw .reconfigurationFailed(complete.rawValue) }
    }
}
