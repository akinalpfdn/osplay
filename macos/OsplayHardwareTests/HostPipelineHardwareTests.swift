import CoreBridge
import CoreGraphics
import Foundation
import Testing

@testable import Osplay

/// Phase 6 acceptance on real hardware: two 2560×1440 at 60 Hz virtual displays with a moving
/// pattern, captured and encoded at the same time. Needs Screen Recording permission for the test
/// host (the debug Osplay app). Kept out of CI like every hardware test.
@MainActor
@Suite("Host pipeline on this Mac", .serialized)
struct HostPipelineHardwareTests {
    private static let peerID = Data("osplay-hardware-pipeline-peer".utf8)
    private static let duration: Duration = .seconds(10)
    /// Pacing is measured after the first frames, once capture and encoders have warmed up.
    private static let warmUp: Duration = .seconds(2)
    static let reportURL = FileManager.default.temporaryDirectory.appending(path: "osplay-host-pipeline.txt")

    @Test("Two 1440p60 streams capture and encode with stable pacing")
    func twoStreamsKeepPace() async throws {
        try #require(CGPreflightScreenCaptureAccess(), "grant Screen Recording to the debug Osplay app first")
        let monitors = (0..<2).map { index in
            MonitorDescription(
                id: UInt8(index), widthPixels: 2560, heightPixels: 1440, refreshMillihertz: 60_000,
                physicalWidthMillimeters: 597, physicalHeightMillimeters: 336, scalePercent: 100,
                name: "Osplay Pipeline Test \(index + 1)")
        }
        let lender = VirtualDisplayLender(provider: CGVirtualDisplayProvider())
        let displays = try await lender.lend(monitors, peerID: Self.peerID)
        defer { lender.returnAll() }
        let patterns = TestPatternWindows()
        patterns.show(on: displays.map(\.displayID))
        defer { patterns.close() }

        let probes = displays.map { _ in PipelineProbe() }
        var pipelines: [(capture: any ScreenCapturing, encoder: any VideoEncoding)] = []
        for (index, display) in displays.enumerated() {
            let probe = probes[index]
            let encoder = try VideoToolboxEncoder(
                EncoderConfiguration(width: 2560, height: 1440, frameRate: 60, bitrateKbps: 22_000, profile: .high)
            ) { result in probe.encoded(result) }
            let queue = DispatchQueue(label: "hardware-test-\(index)", qos: .userInteractive)
            let capture = ScreenCaptureKitCapture(
                CaptureConfiguration(
                    displayID: display.displayID, width: 2560, height: 1440, frameRate: 60, showsCursor: true),
                queue: queue
            ) { event in
                guard case .frame(let frame) = event else { return }
                probe.captured()
                // Called on `queue`, the encoder's single serial caller.
                try? encoder.encode(frame.pixelBuffer, presentationTime: frame.presentationTime, forceKeyframe: false)
            }
            try await capture.start()
            pipelines.append((capture, encoder))
        }

        try await Task.sleep(for: Self.warmUp)
        probes.forEach { $0.reset() }
        try await Task.sleep(for: Self.duration)
        for pipeline in pipelines {
            await pipeline.capture.stop()
            pipeline.encoder.invalidate()
        }

        var report = ""
        for (index, probe) in probes.enumerated() {
            let result = probe.result()
            let pacing = try #require(result.pacing)
            let line =
                "stream \(index): captured \(result.captured), encoded \(result.encoded), "
                + "\(pacing.framesPerSecond) fps, p99 \(pacing.p99IntervalMilliseconds) ms, "
                + "max \(pacing.maximumIntervalMilliseconds) ms"
            report += line + "\n"
            #expect(pacing.framesPerSecond > 58, Comment(rawValue: line))
            #expect(pacing.p99IntervalMilliseconds < 33, Comment(rawValue: line))
            #expect(result.errors == 0, Comment(rawValue: line))
        }
        // The figures, also when every expectation holds (docs/BUILDING.md, hardware tests).
        try report.write(to: Self.reportURL, atomically: true, encoding: .utf8)
    }
}

/// Counts frames and measures capture pacing from the capture queue.
private final class PipelineProbe: @unchecked Sendable {
    // @unchecked: every field is guarded by `lock`.
    private let lock = NSLock()
    private var meter = PacingMeter(windowSize: 1_000)
    private var capturedCount = 0
    private var encodedCount = 0
    private var errorCount = 0

    func captured() {
        let now = Double(DispatchTime.now().uptimeNanoseconds) / 1_000_000_000
        lock.withLock {
            capturedCount += 1
            meter.record(arrivalAt: now)
        }
    }

    func encoded(_ result: Result<EncodedFrame, VideoEncoderError>) {
        lock.withLock {
            if case .success = result { encodedCount += 1 } else { errorCount += 1 }
        }
    }

    func reset() {
        lock.withLock {
            meter = PacingMeter(windowSize: 1_000)
            capturedCount = 0
            encodedCount = 0
            errorCount = 0
        }
    }

    func result() -> (captured: Int, encoded: Int, errors: Int, pacing: PacingMeter.Summary?) {
        lock.withLock { (capturedCount, encodedCount, errorCount, meter.summary) }
    }
}
