import CoreGraphics
import Foundation

/// TCC permissions the app depends on. Onboarding (Phase 11) builds on the same service.
protocol PermissionChecking: Sendable {
    /// Screen Recording, required by ScreenCaptureKit. Checking never prompts.
    var hasScreenRecordingAccess: Bool { get }
    /// Shows the system prompt once per app install; afterwards only System Settings can grant it.
    func requestScreenRecordingAccess() -> Bool
    /// The Screen Recording pane of System Settings.
    var screenRecordingSettingsURL: URL? { get }
}

struct SystemPermissions: PermissionChecking {
    var hasScreenRecordingAccess: Bool { CGPreflightScreenCaptureAccess() }

    func requestScreenRecordingAccess() -> Bool { CGRequestScreenCaptureAccess() }

    var screenRecordingSettingsURL: URL? {
        URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture")
    }
}
