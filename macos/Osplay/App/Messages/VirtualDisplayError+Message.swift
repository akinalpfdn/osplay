import Foundation

extension VirtualDisplayError {
    /// Localized, actionable text for the UI. Services never produce user-facing text themselves.
    var message: String {
        switch self {
        case .invalidConfiguration:
            String(localized: "error.virtualDisplay.invalidConfiguration")
        case .duplicateIdentity:
            String(localized: "error.virtualDisplay.duplicateIdentity")
        case .tooManyDisplays:
            String(localized: "error.virtualDisplay.tooManyDisplays \(Tunables.VirtualDisplay.maximumDisplayCount)")
        case .creationFailed:
            String(localized: "error.virtualDisplay.creationFailed")
        case .settingsRejected:
            String(localized: "error.virtualDisplay.settingsRejected")
        case .resolutionUnavailable:
            String(localized: "error.virtualDisplay.resolutionUnavailable")
        case .displayDestroyed:
            String(localized: "error.virtualDisplay.displayDestroyed")
        case .reconfigurationFailed(let code):
            String(localized: "error.virtualDisplay.reconfigurationFailed \(Int(code))")
        }
    }
}
