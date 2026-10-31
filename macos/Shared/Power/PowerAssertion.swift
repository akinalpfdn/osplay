import Foundation

/// Keeps the device awake and responsive while it is used as a display (DEVPLAN: power, App Nap).
protocol PowerAssertionManaging: Sendable {
    /// Prevents display sleep (and App Nap on the Mac) until the returned token is released.
    func preventDisplaySleep(reason: String) -> any PowerAssertion
}

protocol PowerAssertion: AnyObject, Sendable {
    func release()
}
