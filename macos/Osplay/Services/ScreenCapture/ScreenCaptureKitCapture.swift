import CoreGraphics
import CoreMedia
import Foundation
import OSLog
@preconcurrency import ScreenCaptureKit

/// Captures one display with ScreenCaptureKit: 420v buffers at the stream's pixel size, cursor
/// composited, only frames whose content changed.
final class ScreenCaptureKitCapture: NSObject, ScreenCapturing, @unchecked Sendable {
    // @unchecked: `stream` is only touched in start/stop, which the host session serializes;
    // ScreenCaptureKit calls the output and delegate methods on `queue`.
    private let configuration: CaptureConfiguration
    private let queue: DispatchQueue
    private let output: CaptureOutput
    private var stream: SCStream?

    init(_ configuration: CaptureConfiguration, queue: DispatchQueue, output: @escaping CaptureOutput) {
        self.configuration = configuration
        self.queue = queue
        self.output = output
    }

    func start() async throws(ScreenCaptureError) {
        let display = try await Self.display(configuration.displayID)
        let stream = SCStream(
            filter: SCContentFilter(display: display, excludingWindows: []),
            configuration: Self.streamConfiguration(configuration), delegate: self)
        do {
            try stream.addStreamOutput(self, type: .screen, sampleHandlerQueue: queue)
            try await stream.startCapture()
        } catch {
            throw ScreenCaptureError(error)
        }
        self.stream = stream
    }

    func stop() async {
        guard let stream else { return }
        self.stream = nil
        do {
            try await stream.stopCapture()
        } catch {
            // Already stopped (e.g. by the system); nothing is left to release.
            Log.capture.info("Stopping capture: \(error.localizedDescription, privacy: .public)")
        }
    }

    static func streamConfiguration(_ configuration: CaptureConfiguration) -> SCStreamConfiguration {
        let stream = SCStreamConfiguration()
        stream.width = configuration.width
        stream.height = configuration.height
        stream.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
        stream.colorMatrix = CGDisplayStream.yCbCrMatrix_ITU_R_709_2
        stream.colorSpaceName = CGColorSpace.itur_709
        // A frame that arrives a little early must not be dropped: with an interval of exactly one
        // refresh, display timing jitter makes ScreenCaptureKit skip frames.
        stream.minimumFrameInterval = CMTime(
            seconds: 1 / (configuration.frameRate * Tunables.Capture.frameRateHeadroom), preferredTimescale: 600_000)
        stream.queueDepth = Tunables.Capture.queueDepth
        stream.showsCursor = configuration.showsCursor
        stream.scalesToFit = true
        stream.capturesAudio = false
        return stream
    }

    /// The display becomes visible to ScreenCaptureKit shortly after it is created.
    private static func display(_ displayID: CGDirectDisplayID) async throws(ScreenCaptureError) -> SCDisplay {
        let deadline = ContinuousClock.now + Tunables.Capture.displayLookupTimeout
        while true {
            let content: SCShareableContent
            do {
                content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: false)
            } catch {
                throw ScreenCaptureError(error)
            }
            if let display = content.displays.first(where: { $0.displayID == displayID }) {
                return display
            }
            guard ContinuousClock.now < deadline else { throw .displayNotFound }
            do {
                try await Task.sleep(for: Tunables.Capture.displayLookupInterval)
            } catch {
                throw .displayNotFound
            }
        }
    }
}

extension ScreenCaptureKitCapture: SCStreamOutput {
    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, Self.isNewFrame(sampleBuffer), let pixelBuffer = sampleBuffer.imageBuffer else {
            return
        }
        output(.frame(CapturedFrame(pixelBuffer: pixelBuffer, presentationTime: sampleBuffer.presentationTimeStamp)))
    }

    /// Idle, blank and suspended samples carry no new image.
    static func isNewFrame(_ sampleBuffer: CMSampleBuffer) -> Bool {
        guard
            let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false)
                as? [[SCStreamFrameInfo: Any]],
            let raw = attachments.first?[.status] as? Int,
            let status = SCFrameStatus(rawValue: raw)
        else { return false }
        return status == .complete || status == .started
    }
}

extension ScreenCaptureKitCapture: SCStreamDelegate {
    func stream(_ stream: SCStream, didStopWithError error: any Error) {
        let reason = ScreenCaptureError(error)
        Log.capture.error("Capture stopped: \(String(describing: reason), privacy: .public)")
        output(.stopped(reason))
    }
}

extension ScreenCaptureError {
    /// `SCStreamError.Code.systemStoppedStream`, which the SDK declares only from macOS 15 on.
    static let systemStoppedStreamCode = -3821

    init(_ error: any Error) {
        let error = error as NSError
        guard error.domain == SCStreamErrorDomain else {
            self = .failed(code: error.code)
            return
        }
        switch error.code {
        case SCStreamError.Code.userDeclined.rawValue: self = .permissionDenied
        case SCStreamError.Code.userStopped.rawValue: self = .stoppedByUser
        case Self.systemStoppedStreamCode: self = .stoppedBySystem
        case SCStreamError.Code.noCaptureSource.rawValue, SCStreamError.Code.noDisplayList.rawValue:
            self = .displayNotFound
        default: self = .failed(code: error.code)
        }
    }
}

/// Creates ScreenCaptureKit captures.
struct ScreenCaptureKitCaptureFactory: ScreenCaptureMaking {
    func makeCapture(
        _ configuration: CaptureConfiguration, queue: DispatchQueue, output: @escaping CaptureOutput
    ) -> any ScreenCapturing {
        ScreenCaptureKitCapture(configuration, queue: queue, output: output)
    }
}
