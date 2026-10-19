import CoreGraphics

/// Resolution listing and switching through public CoreGraphics API. Works for any display; the
/// virtual display provider uses it so the private API surface stays minimal.
enum DisplayModes {
    struct Mode {
        let resolution: DisplayResolution
        let mode: CGDisplayMode
    }

    static func available(for displayID: CGDirectDisplayID) -> [Mode] {
        // Without this option macOS hides the 2× variants that HiDPI "looks like" sizes depend on.
        let options = [kCGDisplayShowDuplicateLowResolutionModes: kCFBooleanTrue] as CFDictionary
        guard let modes = CGDisplayCopyAllDisplayModes(displayID, options) as? [CGDisplayMode] else { return [] }

        var seen = Set<DisplayResolution>()
        return
            modes
            .filter { $0.isUsableForDesktopGUI() }
            .map { Mode(resolution: resolution(of: $0), mode: $0) }
            .filter { seen.insert($0.resolution).inserted }
            .sorted { lhs, rhs in
                if lhs.resolution.points.width != rhs.resolution.points.width {
                    return lhs.resolution.points.width > rhs.resolution.points.width
                }
                return lhs.resolution.pixels.width > rhs.resolution.pixels.width
            }
    }

    static func current(for displayID: CGDirectDisplayID) -> Mode? {
        guard let mode = CGDisplayCopyDisplayMode(displayID) else { return nil }
        return Mode(resolution: resolution(of: mode), mode: mode)
    }

    static func activate(
        _ resolution: DisplayResolution, on displayID: CGDirectDisplayID
    )
        throws(VirtualDisplayError)
    {
        guard let target = closest(to: resolution, in: available(for: displayID)) else {
            throw .resolutionUnavailable
        }

        var config: CGDisplayConfigRef?
        let begin = CGBeginDisplayConfiguration(&config)
        guard begin == .success, let config else { throw .reconfigurationFailed(begin.rawValue) }

        let configure = CGConfigureDisplayWithDisplayMode(config, displayID, target.mode, nil)
        guard configure == .success else {
            CGCancelDisplayConfiguration(config)
            throw .reconfigurationFailed(configure.rawValue)
        }
        // Permanent, like a choice in System Settings, so macOS restores it for this display identity.
        let complete = CGCompleteDisplayConfiguration(config, .permanently)
        guard complete == .success else { throw .reconfigurationFailed(complete.rawValue) }
    }

    /// The mode with the same point and pixel size and the nearest refresh rate. The window server
    /// reports its own value for a configured rate (59.951 Hz may come back as 59.95 or 60), so an
    /// exact match fails for the fractional rates Windows monitors use.
    static func closest(to resolution: DisplayResolution, in modes: [Mode]) -> Mode? {
        modes
            .filter { $0.resolution.points == resolution.points && $0.resolution.pixels == resolution.pixels }
            .min { lhs, rhs in
                abs(lhs.resolution.refreshRate - resolution.refreshRate)
                    < abs(rhs.resolution.refreshRate - resolution.refreshRate)
            }
    }

    private static func resolution(of mode: CGDisplayMode) -> DisplayResolution {
        DisplayResolution(
            points: DisplaySize(width: mode.width, height: mode.height),
            pixels: DisplaySize(width: mode.pixelWidth, height: mode.pixelHeight),
            refreshRate: mode.refreshRate
        )
    }
}
