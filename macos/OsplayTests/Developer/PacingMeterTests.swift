import Testing

@testable import Osplay

@Suite("Pacing meter")
struct PacingMeterTests {
    @Test("Steady 60 fps arrivals report 60 fps and 16.7 ms intervals")
    func steadyRate() throws {
        var meter = PacingMeter(windowSize: 120)
        for index in 0..<200 {
            meter.record(arrivalAt: Double(index) / 60)
        }
        let summary = try #require(meter.summary)
        #expect(abs(summary.framesPerSecond - 60) < 0.01)
        #expect(abs(summary.maximumIntervalMilliseconds - 16.667) < 0.01)
    }

    @Test("One stall shows up in the maximum and percentile, not much in the rate")
    func detectsStall() throws {
        var meter = PacingMeter(windowSize: 200)
        var time = 0.0
        for index in 0..<200 {
            time += index == 100 ? 0.1 : 1.0 / 60
            meter.record(arrivalAt: time)
        }
        let summary = try #require(meter.summary)
        #expect(abs(summary.maximumIntervalMilliseconds - 100) < 0.01)
        #expect(summary.p99IntervalMilliseconds < 17)
        #expect(summary.framesPerSecond > 55)
    }

    @Test("No interval before the second frame")
    func needsTwoFrames() {
        var meter = PacingMeter()
        meter.record(arrivalAt: 1)
        #expect(meter.summary == nil)
    }
}
