import Foundation

/// Shows the six-digit pairing code with accept and reject, wherever the user is: the menu bar
/// window is closed most of the time. Phase 11 replaces it with the final pairing sheet.
@MainActor
protocol PairingPrompting: AnyObject {
    func show(code: UInt32, peerName: String, decision: @escaping @MainActor (Bool) -> Void)
    func dismiss()
}

extension UInt32 {
    /// "123 456", as both devices show it.
    var pairingCodeText: String {
        let digits = String(format: "%06u", self)
        return String(digits.prefix(3)) + " " + String(digits.suffix(3))
    }
}
