import CoreBridge
import CoreMedia

/// Converts capture timestamps (CMClock host time, which stops while the Mac sleeps) to the core's
/// clock (which does not), by keeping each frame's age.
enum CaptureClock {
    static func coreMicroseconds(
        fromHostMicroseconds captured: UInt64,
        hostNow: UInt64 = hostMicroseconds(), coreNow: UInt64 = CoreClock.microseconds
    ) -> UInt64 {
        let age = hostNow > captured ? hostNow - captured : 0
        return coreNow > age ? coreNow - age : 0
    }

    static func hostMicroseconds() -> UInt64 {
        let seconds = CMClockGetTime(CMClockGetHostTimeClock()).seconds
        return seconds.isFinite && seconds > 0 ? UInt64(seconds * 1_000_000) : 0
    }
}
