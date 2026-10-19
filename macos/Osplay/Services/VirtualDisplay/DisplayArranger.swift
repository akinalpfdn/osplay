import CoreGraphics

/// Moves displays to arranged positions, the way System Settings › Displays › Arrange would; macOS
/// keeps the arrangement for the displays' identities.
enum DisplayArranger {
    /// `origins` are relative to the top-left corner of the main display, in points.
    @MainActor
    static func apply(_ origins: [(id: CGDirectDisplayID, origin: CGPoint)]) throws(VirtualDisplayError) {
        guard !origins.isEmpty else { return }
        let main = CGDisplayBounds(CGMainDisplayID()).origin
        var config: CGDisplayConfigRef?
        let begin = CGBeginDisplayConfiguration(&config)
        guard begin == .success, let config else { throw .reconfigurationFailed(begin.rawValue) }
        for display in origins {
            let result = CGConfigureDisplayOrigin(
                config, display.id, Int32((main.x + display.origin.x).rounded()),
                Int32((main.y + display.origin.y).rounded()))
            guard result == .success else {
                CGCancelDisplayConfiguration(config)
                throw .reconfigurationFailed(result.rawValue)
            }
        }
        let complete = CGCompleteDisplayConfiguration(config, .permanently)
        guard complete == .success else { throw .reconfigurationFailed(complete.rawValue) }
    }

    /// The main display's size in points, the reference every arrangement is relative to.
    @MainActor
    static var mainDisplaySize: CGSize { CGDisplayBounds(CGMainDisplayID()).size }
}
