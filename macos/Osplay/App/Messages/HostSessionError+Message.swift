import Foundation

extension HostSessionError {
    /// Localized, actionable text for the UI.
    var message: String {
        switch self {
        case .screenRecordingPermissionMissing:
            String(localized: "error.host.screenRecordingMissing")
        case .display(let error):
            error.message
        case .stream(.capture(let error)):
            error.message
        case .stream(.encoder(let error)):
            error.message
        case .session(let error):
            String(localized: "error.host.session \(Int(error.code))")
        }
    }

    /// Whether the fix is in System Settings → Privacy & Security → Screen Recording.
    var needsScreenRecordingPermission: Bool {
        self == .screenRecordingPermissionMissing || self == .stream(.capture(.permissionDenied))
    }
}

extension ScreenCaptureError {
    var message: String {
        switch self {
        case .permissionDenied:
            String(localized: "error.capture.permissionRevoked")
        case .displayNotFound:
            String(localized: "error.capture.displayNotFound")
        case .stoppedByUser:
            String(localized: "error.capture.stoppedByUser")
        case .stoppedBySystem:
            String(localized: "error.capture.stoppedBySystem")
        case .failed(let code):
            String(localized: "error.capture.failed \(code)")
        }
    }
}

extension VideoEncoderError {
    var message: String {
        switch self {
        case .creationFailed(let status), .configurationRejected(let status):
            String(localized: "error.encoder.unavailable \(Int(status))")
        case .encodeFailed(let status):
            String(localized: "error.encoder.failed \(Int(status))")
        case .invalidOutput, .invalidated:
            String(localized: "error.encoder.failed \(0)")
        }
    }
}
