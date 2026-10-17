import CoreBridge
import CoreMedia
import Foundation
import OSLog

/// Why one stream stopped on its own.
enum StreamFailure: Error, Equatable, Sendable {
    case capture(ScreenCaptureError)
    case encoder(VideoEncoderError)
}

/// Capture → encode → core for one stream. Frames never leave `queue` before they are encoded, and
/// pixels are never copied: ScreenCaptureKit's IOSurface goes straight into VideoToolbox.
final class StreamPipeline: @unchecked Sendable {
    // @unchecked: mutable state (`lastFrame`, `keyframePending`, `lastPresentationTime`,
    // `stopped`) is only touched on `queue`; `capture` and `encoder` are set once in `start`.
    let stream: StreamConfiguration
    let displayID: CGDirectDisplayID
    private let queue: DispatchQueue
    private let captures: any ScreenCaptureMaking
    private let encoders: any VideoEncoderMaking
    private let sink: @Sendable (AccessUnit) -> Void
    private let onFailure: @Sendable (StreamFailure) -> Void
    private var capture: (any ScreenCapturing)?
    private var encoder: (any VideoEncoding)?
    private var lastFrame: CapturedFrame?
    private var lastPresentationTime = CMTime.negativeInfinity
    private var keyframePending = true
    private var stopped = false

    init(
        stream: StreamConfiguration, displayID: CGDirectDisplayID, captures: any ScreenCaptureMaking,
        encoders: any VideoEncoderMaking, sink: @escaping @Sendable (AccessUnit) -> Void,
        onFailure: @escaping @Sendable (StreamFailure) -> Void
    ) {
        self.stream = stream
        self.displayID = displayID
        self.captures = captures
        self.encoders = encoders
        self.sink = sink
        self.onFailure = onFailure
        queue = DispatchQueue(label: "com.akinalpfdn.osplay.stream\(stream.streamID)", qos: .userInteractive)
    }

    /// Creates the encoder and starts capturing; the first frame is a keyframe.
    func start() async throws(StreamFailure) {
        let streamID = stream.streamID
        let sink = sink
        let onFailure = onFailure
        do {
            encoder = try encoders.makeEncoder(
                EncoderConfiguration(
                    width: stream.width, height: stream.height, frameRate: stream.refreshRate,
                    bitrateKbps: stream.bitrateKbps, profile: stream.profile)
            ) { result in
                switch result {
                case .success(let frame):
                    sink(
                        AccessUnit(
                            streamID: streamID, isKeyframe: frame.isKeyframe, carriesParameterSets: frame.isKeyframe,
                            captureTimeMicroseconds: frame.captureTimeMicroseconds, data: frame.data))
                case .failure(let error):
                    onFailure(.encoder(error))
                }
            }
        } catch {
            throw .encoder(error)
        }
        let capture = captures.makeCapture(
            CaptureConfiguration(
                displayID: displayID, width: stream.width, height: stream.height, frameRate: stream.refreshRate,
                showsCursor: stream.cursorMode == .inVideo),
            queue: queue
        ) { [weak self] event in
            self?.handle(event)
        }
        self.capture = capture
        do {
            try await capture.start()
        } catch {
            throw .capture(error)
        }
    }

    /// The next frame becomes a keyframe. An unchanged screen produces no frames, so the last one
    /// is encoded again right away; otherwise the viewer would wait until something moves.
    func requestKeyframe() {
        queue.async { [weak self] in
            guard let self, !stopped else { return }
            keyframePending = true
            if let lastFrame {
                encode(lastFrame.pixelBuffer, at: max(Self.hostTime(), nextPresentationTime))
            }
        }
    }

    func stop() async {
        await capture?.stop()
        queue.sync {
            stopped = true
            lastFrame = nil
        }
        encoder?.invalidate()
    }

    private func handle(_ event: CaptureEvent) {
        guard !stopped else { return }
        switch event {
        case .frame(let frame):
            lastFrame = frame
            encode(frame.pixelBuffer, at: max(frame.presentationTime, nextPresentationTime))
        case .stopped(let error):
            stopped = true
            lastFrame = nil
            onFailure(.capture(error))
        }
    }

    private func encode(_ pixelBuffer: CVPixelBuffer, at time: CMTime) {
        guard let encoder else { return }
        do {
            try encoder.encode(pixelBuffer, presentationTime: time, forceKeyframe: keyframePending)
            keyframePending = false
            lastPresentationTime = time
        } catch {
            stopped = true
            onFailure(.encoder(error))
        }
    }

    /// The encoder needs strictly increasing timestamps, also for re-sent frames.
    private var nextPresentationTime: CMTime {
        lastPresentationTime.isNumeric
            ? CMTimeAdd(lastPresentationTime, CMTime(value: 1, timescale: 1_000_000)) : .negativeInfinity
    }

    private static func hostTime() -> CMTime {
        CMClockGetTime(CMClockGetHostTimeClock())
    }
}
