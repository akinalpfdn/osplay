import CoreBridge
import CoreMedia
import CoreVideo
import Foundation

/// A decoded frame: an IOSurface-backed buffer the presenter shows without copying.
struct DecodedFrame: @unchecked Sendable {
    // @unchecked: the decoder never writes to a buffer after delivering it.
    let streamID: UInt8
    let pixelBuffer: CVPixelBuffer
    /// Host's core clock at capture, microseconds.
    let captureTimeMicroseconds: UInt64
}

enum VideoDecoderError: Error, Equatable, Sendable {
    /// Waiting for a keyframe with parameter sets (start, or after an error); the frame was dropped.
    case keyframeNeeded
    /// The access unit is not valid Annex B.
    case invalidData
    case sessionCreationFailed(OSStatus)
    /// VideoToolbox rejected or failed to decode the frame; later frames wait for a keyframe.
    case decodeFailed(OSStatus)
}

/// Receives decoded frames, or why one could not be decoded, on the decoder's own thread. Every
/// error means "ask the host for a keyframe".
typealias DecoderOutput = @Sendable (Result<DecodedFrame, VideoDecoderError>) -> Void

/// An H.264 decoder for one stream. `decode` must be called from one serial queue.
protocol VideoDecoding: AnyObject, Sendable {
    func decode(_ unit: AccessUnit)
    /// Stops decoding; no output follows.
    func invalidate()
}

/// Creates decoders; injected so the viewer session can be tested without VideoToolbox.
protocol VideoDecoderMaking: Sendable {
    func makeDecoder(output: @escaping DecoderOutput) -> any VideoDecoding
}
