import AppKit
import CoreBridge
import CoreGraphics

/// The display this Mac offers when it is used as a viewer (DEVPLAN: the MacBook's screen).
enum LocalMonitor {
    /// The built-in display, or the main display on a Mac without one.
    static func offered() -> (description: MonitorDescription, displayID: CGDirectDisplayID)? {
        let screens = NSScreen.screens
        guard
            let screen = screens.first(where: { $0.displayID.map { CGDisplayIsBuiltin($0) != 0 } ?? false })
                ?? NSScreen.main,
            let displayID = screen.displayID, let mode = CGDisplayCopyDisplayMode(displayID)
        else { return nil }
        let millimeters = CGDisplayScreenSize(displayID)
        // Built-in panels report 0 Hz; they run at 60 Hz unless ProMotion, which macOS reports.
        let refresh = mode.refreshRate > 0 ? mode.refreshRate : Tunables.Viewer.defaultRefreshRate
        let description = MonitorDescription(
            id: 0, widthPixels: mode.pixelWidth, heightPixels: mode.pixelHeight,
            refreshMillihertz: UInt32((refresh * 1000).rounded()),
            physicalWidthMillimeters: Int(millimeters.width.rounded()),
            physicalHeightMillimeters: Int(millimeters.height.rounded()),
            scalePercent: Int((screen.backingScaleFactor * 100).rounded()), name: screen.localizedName)
        return (description, displayID)
    }
}
