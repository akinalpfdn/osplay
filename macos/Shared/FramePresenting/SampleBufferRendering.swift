import AVFoundation
import CoreBridge
import CoreGraphics
import OSLog

// Presenting with AVSampleBufferDisplayLayer, shared by the Mac's windows and the iPad's view.

/// Maps a cursor position in stream pixels onto the aspect-fitted video inside a view, in
/// top-down coordinates.
enum CursorGeometry {
    static func frame(position: CursorPosition, shape: CursorShape, streamSize: CGSize, bounds: CGRect) -> CGRect? {
        guard streamSize.width > 0, streamSize.height > 0, bounds.width > 0, bounds.height > 0 else { return nil }
        let video = AVMakeRect(aspectRatio: streamSize, insideRect: bounds)
        let scale = video.width / streamSize.width
        return CGRect(
            x: video.minX + (CGFloat(position.x) - CGFloat(shape.hotspotX)) * scale,
            y: video.minY + (CGFloat(position.y) - CGFloat(shape.hotspotY)) * scale,
            width: CGFloat(shape.width) * scale, height: CGFloat(shape.height) * scale)
    }
}

/// Hands frames to the layer's renderer from the decoder's thread.
final class RendererSink: FrameSink, @unchecked Sendable {
    // @unchecked: AVSampleBufferVideoRenderer accepts samples from any thread; one decoder
    // queue calls `enqueue`.
    private let renderer: AVSampleBufferVideoRenderer

    init(renderer: AVSampleBufferVideoRenderer) {
        self.renderer = renderer
    }

    func enqueue(_ frame: DecodedFrame) {
        if renderer.status == .failed {
            Log.viewer.error("Renderer failed: \(String(describing: self.renderer.error), privacy: .public)")
            renderer.flush()
        }
        guard let sample = Self.sampleBuffer(frame) else { return }
        renderer.enqueue(sample)
    }

    private static func sampleBuffer(_ frame: DecodedFrame) -> CMSampleBuffer? {
        var format: CMVideoFormatDescription?
        guard
            CMVideoFormatDescriptionCreateForImageBuffer(
                allocator: nil, imageBuffer: frame.pixelBuffer, formatDescriptionOut: &format) == noErr,
            let format
        else { return nil }
        var timing = CMSampleTimingInfo(
            duration: .invalid,
            presentationTimeStamp: CMTime(value: CMTimeValue(frame.captureTimeMicroseconds), timescale: 1_000_000),
            decodeTimeStamp: .invalid)
        var sample: CMSampleBuffer?
        guard
            CMSampleBufferCreateReadyWithImageBuffer(
                allocator: nil, imageBuffer: frame.pixelBuffer, formatDescription: format, sampleTiming: &timing,
                sampleBufferOut: &sample) == noErr,
            let sample
        else { return nil }
        // Shown as soon as it is enqueued, regardless of its timestamp.
        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: true),
            CFArrayGetCount(attachments) > 0
        {
            let dictionary = unsafeBitCast(CFArrayGetValueAtIndex(attachments, 0), to: CFMutableDictionary.self)
            CFDictionarySetValue(
                dictionary, Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                Unmanaged.passUnretained(kCFBooleanTrue).toOpaque())
        }
        return sample
    }
}

/// A separate-cursor stream's cursor shape as an image for a layer.
enum CursorImage {
    static func make(_ shape: CursorShape) -> CGImage? {
        guard let provider = CGDataProvider(data: shape.bgra as CFData) else { return nil }
        return CGImage(
            width: shape.width, height: shape.height, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: shape.width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB) ?? CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(
                rawValue: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}
