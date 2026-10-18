import OsplayCore

/// The core's monotonic clock: the time base of `AccessUnit.captureTimeMicroseconds`.
public enum CoreClock {
    /// Microseconds; keeps counting while the Mac sleeps, unlike CMClock's host time.
    public static var microseconds: UInt64 { osplay_clock_microseconds() }
}
