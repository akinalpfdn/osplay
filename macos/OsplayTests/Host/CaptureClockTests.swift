import Testing

@testable import Osplay

@Suite("Capture clock")
struct CaptureClockTests {
    @Test("A frame keeps its age when moved to the core's clock")
    func keepsAge() {
        let converted = CaptureClock.coreMicroseconds(
            fromHostMicroseconds: 5_000_000, hostNow: 5_010_000, coreNow: 9_000_000_000)
        #expect(converted == 8_999_990_000)
    }

    @Test("Timestamps from the future count as captured now")
    func clampsFutureTimestamps() {
        #expect(CaptureClock.coreMicroseconds(fromHostMicroseconds: 7, hostNow: 5, coreNow: 100) == 100)
    }

    @Test("Live clocks give a capture time just before now")
    func liveClocks() {
        let now = CaptureClock.hostMicroseconds()
        let converted = CaptureClock.coreMicroseconds(fromHostMicroseconds: now - 20_000)
        let core = CaptureClock.coreMicroseconds(fromHostMicroseconds: CaptureClock.hostMicroseconds())
        #expect(core - converted >= 19_000 && core - converted < 100_000)
    }
}
