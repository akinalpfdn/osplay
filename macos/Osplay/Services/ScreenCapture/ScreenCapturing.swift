import CoreGraphics
import CoreMedia
import CoreVideo
import Dispatch

/// What to capture from one display.
struct CaptureConfiguration: Hashable, Sendable {
    let displayID: CGDirectDisplayID
    /// Output size; ScreenCaptureKit scales the display's backing to it.
    let width: Int
    let height: Int
    let frameRate: Double
    let showsCursor: Bool
}

/// A new screen image: an IOSurface-backed 420v buffer that goes to the encoder without copies.
struct CapturedFrame: @unchecked Sendable {
    // @unchecked: ScreenCaptureKit never writes to a buffer after delivering it, and the
    // pipeline only reads it.
    let pixelBuffer: CVPixelBuffer
    /// Host clock (mach absolute time).
    let presentationTime: CMTime
}

enum ScreenCaptureError: Error, Equatable, Sendable {
    /// Screen Recording permission is missing or was revoked.
    case permissionDenied
    case displayNotFound
    /// The user stopped sharing from the system's screen-sharing menu.
    case stoppedByUser
    /// macOS stopped the capture (e.g. the display went away).
    case stoppedBySystem
    case failed(code: Int)
}

/// Events from a running capture, delivered on the queue given at creation.
enum CaptureEvent: Sendable {
    /// The screen changed. Unchanged screens produce no events; the viewer keeps the last frame.
    case frame(CapturedFrame)
    /// Capture ended on its own; no further events follow.
    case stopped(ScreenCaptureError)
}

typealias CaptureOutput = @Sendable (CaptureEvent) -> Void

/// Captures one display.
protocol ScreenCapturing: AnyObject, Sendable {
    func start() async throws(ScreenCaptureError)
    /// Stops without a `.stopped` event.
    func stop() async
}

/// Creates captures; injected so the host session can be tested without ScreenCaptureKit.
protocol ScreenCaptureMaking: Sendable {
    func makeCapture(
        _ configuration: CaptureConfiguration, queue: DispatchQueue, output: @escaping CaptureOutput
    ) -> any ScreenCapturing
}
