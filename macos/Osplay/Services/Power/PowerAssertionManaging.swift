import Foundation
import IOKit.pwr_mgt
import OSLog

/// Keeps the Mac awake and responsive while it is used as a display (DEVPLAN: power, App Nap).
protocol PowerAssertionManaging: Sendable {
    /// Prevents display sleep and App Nap until the returned token is released.
    func preventDisplaySleep(reason: String) -> any PowerAssertion
}

protocol PowerAssertion: AnyObject, Sendable {
    func release()
}

struct SystemPower: PowerAssertionManaging {
    func preventDisplaySleep(reason: String) -> any PowerAssertion {
        SystemPowerAssertion(reason: reason)
    }
}

private final class SystemPowerAssertion: PowerAssertion, @unchecked Sendable {
    // @unchecked: state is guarded by `lock`.
    private let lock = NSLock()
    private var assertionID: IOPMAssertionID = 0
    private var activity: NSObjectProtocol?

    init(reason: String) {
        var id: IOPMAssertionID = 0
        let status = IOPMAssertionCreateWithName(
            kIOPMAssertionTypePreventUserIdleDisplaySleep as CFString, IOPMAssertionLevel(kIOPMAssertionLevelOn),
            reason as CFString, &id)
        if status == kIOReturnSuccess {
            assertionID = id
        } else {
            Log.viewer.error("Display sleep assertion failed: \(status)")
        }
        activity = ProcessInfo.processInfo.beginActivity(
            options: [.userInitiated, .latencyCritical, .idleDisplaySleepDisabled], reason: reason)
    }

    deinit {
        release()
    }

    func release() {
        let (id, token) = lock.withLock {
            defer {
                assertionID = 0
                activity = nil
            }
            return (assertionID, activity)
        }
        if id != 0 {
            IOPMAssertionRelease(id)
        }
        if let token {
            ProcessInfo.processInfo.endActivity(token)
        }
    }
}
