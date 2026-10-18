#if DEBUG
    import Foundation

    /// Frame arrival statistics for one stream, over a sliding window of recent intervals.
    struct PacingMeter {
        struct Summary: Equatable {
            let framesPerSecond: Double
            let meanIntervalMilliseconds: Double
            /// 99th percentile of the frame-to-frame interval.
            let p99IntervalMilliseconds: Double
            let maximumIntervalMilliseconds: Double
        }

        private let windowSize: Int
        private var intervals: [Double] = []
        private var nextSlot = 0
        private var lastArrival: Double?

        init(windowSize: Int = Tunables.Developer.pacingWindow) {
            self.windowSize = windowSize
        }

        /// `seconds` on a monotonic clock.
        mutating func record(arrivalAt seconds: Double) {
            defer { lastArrival = seconds }
            guard let lastArrival else { return }
            let interval = (seconds - lastArrival) * 1000
            if intervals.count < windowSize {
                intervals.append(interval)
            } else {
                intervals[nextSlot] = interval
            }
            nextSlot = (nextSlot + 1) % windowSize
        }

        var summary: Summary? {
            guard !intervals.isEmpty else { return nil }
            let sorted = intervals.sorted()
            let mean = sorted.reduce(0, +) / Double(sorted.count)
            let p99Index = min(sorted.count - 1, Int((Double(sorted.count) * 0.99).rounded(.up)) - 1)
            return Summary(
                framesPerSecond: mean > 0 ? 1000 / mean : 0, meanIntervalMilliseconds: mean,
                p99IntervalMilliseconds: sorted[max(p99Index, 0)], maximumIntervalMilliseconds: sorted[sorted.count - 1]
            )
        }
    }
#endif
