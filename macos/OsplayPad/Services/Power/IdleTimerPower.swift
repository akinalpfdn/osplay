import Foundation
import UIKit

/// Keeps the iPad's screen on while frames are shown: the idle timer is off until released.
struct IdleTimerPower: PowerAssertionManaging {
    func preventDisplaySleep(reason: String) -> any PowerAssertion {
        IdleTimerAssertion()
    }
}

private final class IdleTimerAssertion: PowerAssertion, @unchecked Sendable {
    // @unchecked: `released` is guarded by `lock`.
    private let lock = NSLock()
    private var released = false

    init() {
        Task { @MainActor in UIApplication.shared.isIdleTimerDisabled = true }
    }

    func release() {
        let first = lock.withLock {
            defer { released = true }
            return !released
        }
        guard first else { return }
        Task { @MainActor in UIApplication.shared.isIdleTimerDisabled = false }
    }
}
