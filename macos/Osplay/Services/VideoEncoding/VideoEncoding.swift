import CoreBridge
import CoreMedia
import CoreVideo
import Foundation

/// Settings of one H.264 encoder; protocol v1 streams are H.264 Annex B without B-frames.
struct EncoderConfiguration: Hashable, Sendable {
    let width: Int
    let height: Int
    let frameRate: Double
    let bitrateKbps: Int
    let profile: H264Profile
}

/// One encoded frame, ready for the wire.
struct EncodedFrame: Sendable {
    /// Annex B; keyframes start with SPS and PPS.
    let data: Data
    let isKeyframe: Bool
    /// Capture time on the host clock (mach absolute time), in microseconds.
    let captureTimeMicroseconds: UInt64
}

enum VideoEncoderError: Error, Equatable, Sendable {
    case creationFailed(OSStatus)
    case configurationRejected(OSStatus)
    case encodeFailed(OSStatus)
    case invalidOutput
    case invalidated
}

/// Receives every encoded frame, or the error that ended encoding, on the encoder's own thread.
typealias EncoderOutput = @Sendable (Result<EncodedFrame, VideoEncoderError>) -> Void

/// A realtime H.264 encoder. `encode` must be called from one serial queue; output arrives in
/// encode order.
protocol VideoEncoding: AnyObject, Sendable {
    /// Encodes `pixelBuffer` captured at `presentationTime` (host clock). The buffer is retained by
    /// the encoder only until it has been encoded.
    func encode(
        _ pixelBuffer: CVPixelBuffer, presentationTime: CMTime, forceKeyframe: Bool
    ) throws(VideoEncoderError)

    /// Encodes the following frames at this average bitrate (adapted to the link).
    func setBitrate(kbps: Int)

    /// Returns once every frame submitted so far has been output.
    func flush()

    /// Stops encoding; frames still in flight are dropped and no output follows.
    func invalidate()
}

/// Creates encoders; injected so the host session can be tested without VideoToolbox.
protocol VideoEncoderMaking: Sendable {
    func makeEncoder(
        _ configuration: EncoderConfiguration, output: @escaping EncoderOutput
    ) throws(VideoEncoderError) -> any VideoEncoding
}
