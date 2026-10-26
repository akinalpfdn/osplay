import CoreBridge
import Foundation

/// Counters of one viewer stream, for diagnostics.
struct ViewerStreamStatistics: Equatable, Sendable {
    var framesPresented: UInt64 = 0
    var decodeErrors: UInt64 = 0
    /// From the first error to the next presented frame, for the latest recovery.
    var lastRecoveryMilliseconds: Double?
}

/// Core → decode → present for one stream. Access units are decoded on `queue`; decoded frames
/// go straight from the decoder's thread to the presenter's sink.
final class ViewerStreamPipeline: @unchecked Sendable {
    // @unchecked: `decoder` is set once in init and used on `queue`; the counters and the debug
    // corruption flag are guarded by `lock`.
    let stream: StreamConfiguration
    private let queue: DispatchQueue
    private let lock = NSLock()
    private var decoder: (any VideoDecoding)?
    private var statistics = ViewerStreamStatistics()
    private var failingSince: ContinuousClock.Instant?
    private var lastKeyframeRequest: ContinuousClock.Instant?
    private var corruptNext = false

    init(
        stream: StreamConfiguration, decoders: any VideoDecoderMaking, sink: any FrameSink,
        needsKeyframe: @escaping @Sendable (UInt8) -> Void
    ) {
        self.stream = stream
        queue = DispatchQueue(label: "com.akinalpfdn.osplay.viewer\(stream.streamID)", qos: .userInteractive)
        let streamID = stream.streamID
        decoder = decoders.makeDecoder { [weak self] result in
            switch result {
            case .success(let frame):
                self?.presented()
                sink.enqueue(frame)
            case .failure:
                if self?.failed() == true {
                    needsKeyframe(streamID)
                }
            }
        }
    }

    func decode(_ unit: AccessUnit) {
        let unit = lock.withLock {
            defer { corruptNext = false }
            return corruptNext ? Self.corrupted(unit) : unit
        }
        queue.async { [decoder] in decoder?.decode(unit) }
    }

    func stop() {
        queue.sync {
            decoder?.invalidate()
            decoder = nil
        }
    }

    var currentStatistics: ViewerStreamStatistics { lock.withLock { statistics } }

    /// Debug: the next access unit loses most of its slice data, as a broken encoder would send it.
    func corruptNextFrame() {
        lock.withLock { corruptNext = true }
    }

    private func presented() {
        lock.withLock {
            statistics.framesPresented += 1
            if let since = failingSince {
                statistics.lastRecoveryMilliseconds = Self.milliseconds(ContinuousClock.now - since)
                failingSince = nil
            }
        }
    }

    /// Whether to ask for a keyframe now; requests are spaced so a burst of errors sends one.
    private func failed() -> Bool {
        lock.withLock {
            let now = ContinuousClock.now
            statistics.decodeErrors += 1
            failingSince = failingSince ?? now
            if let last = lastKeyframeRequest, now - last < Tunables.Viewer.keyframeRequestSpacing {
                return false
            }
            lastKeyframeRequest = now
            return true
        }
    }

    private static func corrupted(_ unit: AccessUnit) -> AccessUnit {
        AccessUnit(
            streamID: unit.streamID, isKeyframe: false, carriesParameterSets: false, frameID: unit.frameID,
            captureTimeMicroseconds: unit.captureTimeMicroseconds, data: Data(unit.data.prefix(unit.data.count / 3)))
    }

    private static func milliseconds(_ duration: Duration) -> Double {
        Double(duration.components.seconds) * 1000 + Double(duration.components.attoseconds) / 1e15
    }
}
