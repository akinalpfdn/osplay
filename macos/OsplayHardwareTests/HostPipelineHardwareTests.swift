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
        let size = (width: 2560, height: 1440, refreshMillihertz: UInt32(60_000))
        try await measure([size, size], reportURL: Self.reportURL)
    }

    /// The developer's Windows viewer: a 1440p 120 Hz and a 1080p 60 Hz monitor (Phase 9 test). The
    /// host caps both at 60 Hz; uncapped, the 1440p display captured at only ~80 fps on an M3 Air.
    @Test("The Phase 9 viewer's 1440p and 1080p monitors stream at a steady 60 fps")
    func windowsViewerLayoutKeepsPace() async throws {
        let sizes = [(2560, 1440, UInt32(120_000)), (1920, 1080, UInt32(60_000))].map { width, height, refresh in
            (width: width, height: height, refreshMillihertz: min(refresh, Tunables.Host.maximumRefreshMillihertz))
        }
        try await measure(
            sizes, reportURL: FileManager.default.temporaryDirectory.appending(path: "osplay-host-pipeline-mixed.txt"))
    }

    private func measure(
        _ sizes: [(width: Int, height: Int, refreshMillihertz: UInt32)], reportURL: URL
    ) async throws {
        try #require(CGPreflightScreenCaptureAccess(), "grant Screen Recording to the debug Osplay app first")
        let monitors = sizes.enumerated().map { index, size in
            MonitorDescription(
                id: UInt8(index), widthPixels: size.width, heightPixels: size.height,
                refreshMillihertz: size.refreshMillihertz,
                physicalWidthMillimeters: 597, physicalHeightMillimeters: 336, scalePercent: 100,
                name: "Osplay Pipeline Test \(index + 1)")
        }
        let lender = VirtualDisplayLender(provider: CGVirtualDisplayProvider())
        let displays = try await lender.lend(monitors, peerID: Self.peerID, hiDPI: .automatic)
        defer { lender.returnAll() }
        let patterns = TestPatternWindows()
        patterns.show(on: displays.map(\.displayID))
        defer { patterns.close() }

        let probes = displays.map { _ in PipelineProbe() }
        var pipelines: [(capture: any ScreenCapturing, encoder: any VideoEncoding)] = []
        for (index, display) in displays.enumerated() {
            let probe = probes[index]
            let size = sizes[index]
            let frameRate = Double(size.refreshMillihertz) / 1000
            let encoder = try VideoToolboxEncoder(
                EncoderConfiguration(
                    width: size.width, height: size.height, frameRate: frameRate, bitrateKbps: 22_000, profile: .high)
            ) { result in probe.encoded(result) }
            let queue = DispatchQueue(label: "hardware-test-\(index)", qos: .userInteractive)
            let capture = ScreenCaptureKitCapture(
                CaptureConfiguration(
                    displayID: display.displayID, width: size.width, height: size.height, frameRate: frameRate,
                    showsCursor: true),
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
            let expected = Double(sizes[index].refreshMillihertz) / 1000
            let line =
                "stream \(index): captured \(result.captured), encoded \(result.encoded), "
                + "\(pacing.framesPerSecond) fps, p99 \(pacing.p99IntervalMilliseconds) ms, "
                + "max \(pacing.maximumIntervalMilliseconds) ms, encode p99 \(result.encodeP99Milliseconds) ms"
            report += line + "\n"
            #expect(pacing.framesPerSecond > expected - 2, Comment(rawValue: line))
            #expect(pacing.p99IntervalMilliseconds < 2000 / expected, Comment(rawValue: line))
            #expect(result.errors == 0, Comment(rawValue: line))
        }
        // The figures, also when every expectation holds (docs/BUILDING.md, hardware tests).
        try report.write(to: reportURL, atomically: true, encoding: .utf8)
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
    private var pendingCaptures: [Double] = []
    private var encodeDelays: [Double] = []

    func captured() {
        let now = Double(DispatchTime.now().uptimeNanoseconds) / 1_000_000_000
        lock.withLock {
            capturedCount += 1
            meter.record(arrivalAt: now)
            pendingCaptures.append(now)
        }
    }

    func encoded(_ result: Result<EncodedFrame, VideoEncoderError>) {
        let now = Double(DispatchTime.now().uptimeNanoseconds) / 1_000_000_000
        lock.withLock {
            if case .success = result { encodedCount += 1 } else { errorCount += 1 }
            // Frames come out in capture order (no B-frames), so the oldest pending one is this one.
            if !pendingCaptures.isEmpty {
                encodeDelays.append((now - pendingCaptures.removeFirst()) * 1000)
            }
        }
    }

    func reset() {
        lock.withLock {
            meter = PacingMeter(windowSize: 1_000)
            capturedCount = 0
            encodedCount = 0
            errorCount = 0
            encodeDelays = []
        }
    }

    func result() -> (
        captured: Int, encoded: Int, errors: Int, pacing: PacingMeter.Summary?, encodeP99Milliseconds: Double
    ) {
        lock.withLock {
            let sorted = encodeDelays.sorted()
            let p99 = sorted.isEmpty ? 0 : sorted[Int(Double(sorted.count - 1) * 0.99)]
            return (capturedCount, encodedCount, errorCount, meter.summary, p99)
        }
    }
}
