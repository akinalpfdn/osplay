import CoreMedia
import CoreVideo
import Foundation
import Testing

@testable import Osplay

@Suite("VideoToolbox encoder")
struct VideoToolboxEncoderTests {
    private let configuration = EncoderConfiguration(
        width: 640, height: 360, frameRate: 60, bitrateKbps: 2_000, profile: .high)

    @Test("Keyframes carry SPS and PPS; other frames are plain Annex B slices")
    func encodesAnnexB() throws {
        let collector = FrameCollector()
        let encoder = try VideoToolboxEncoder(configuration) { collector.append($0) }
        for index in 0..<12 {
            let buffer = try makeFrame(width: 640, height: 360, shade: UInt8(index * 20))
            try encoder.encode(
                buffer, presentationTime: CMTime(value: CMTimeValue(index), timescale: 60),
                forceKeyframe: index == 0 || index == 8)
        }
        encoder.flush()
        encoder.invalidate()

        let frames = try collector.frames.map { try $0.get() }
        #expect(frames.count == 12)
        let keyframes = frames.enumerated().filter { $0.element.isKeyframe }.map(\.offset)
        #expect(keyframes == [0, 8])
        for frame in frames {
            let types = nalTypes(frame.data)
            #expect(frame.data.starts(with: AnnexB.startCode))
            if frame.isKeyframe {
                #expect(Array(types.prefix(2)) == [7, 8], "SPS and PPS lead every keyframe")
                #expect(types.contains(5), "an IDR slice")
            } else {
                #expect(!types.contains(7) && !types.contains(5))
            }
        }
        #expect(frames[1].captureTimeMicroseconds == 16_666)
    }

    @Test("An invalidated encoder refuses frames and stays silent")
    func invalidatedEncoderRefusesFrames() throws {
        let collector = FrameCollector()
        let encoder = try VideoToolboxEncoder(configuration) { collector.append($0) }
        encoder.invalidate()
        let buffer = try makeFrame(width: 640, height: 360, shade: 0)
        #expect(throws: VideoEncoderError.invalidated) {
            try encoder.encode(buffer, presentationTime: .zero, forceKeyframe: true)
        }
        #expect(collector.frames.isEmpty)
    }

    @Test("Bitrates follow the resolution and stay within limits")
    func bitrateTable() {
        #expect(QualityPreset.high.bitrateKbps(width: 2560, height: 1440, frameRate: 60) == 22_118)
        #expect(QualityPreset.high.bitrateKbps(width: 640, height: 480, frameRate: 24) == 2_000)
        #expect(QualityPreset.high.bitrateKbps(width: 7680, height: 4320, frameRate: 240) == 80_000)
    }
}

private final class FrameCollector: @unchecked Sendable {
    private let lock = NSLock()
    private var collected: [Result<EncodedFrame, VideoEncoderError>] = []

    func append(_ result: Result<EncodedFrame, VideoEncoderError>) {
        lock.withLock { collected.append(result) }
    }

    var frames: [Result<EncodedFrame, VideoEncoderError>] { lock.withLock { collected } }
}

/// An IOSurface-backed 420v frame of one luma shade, like ScreenCaptureKit delivers.
private func makeFrame(width: Int, height: Int, shade: UInt8) throws -> CVPixelBuffer {
    var buffer: CVPixelBuffer?
    let attributes = [kCVPixelBufferIOSurfacePropertiesKey: [:]] as CFDictionary
    let status = CVPixelBufferCreate(
        nil, width, height, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, attributes, &buffer)
    let frame = try #require(status == kCVReturnSuccess ? buffer : nil)
    CVPixelBufferLockBaseAddress(frame, [])
    defer { CVPixelBufferUnlockBaseAddress(frame, []) }
    for plane in 0..<CVPixelBufferGetPlaneCount(frame) {
        let base = try #require(CVPixelBufferGetBaseAddressOfPlane(frame, plane))
        let size = CVPixelBufferGetBytesPerRowOfPlane(frame, plane) * CVPixelBufferGetHeightOfPlane(frame, plane)
        memset(base, Int32(plane == 0 ? shade : 128), size)
    }
    return frame
}

/// NAL unit types in an Annex B buffer, in order.
private func nalTypes(_ data: Data) -> [UInt8] {
    let bytes = [UInt8](data)
    var types: [UInt8] = []
    var index = 0
    while index + 4 < bytes.count {
        if bytes[index] == 0, bytes[index + 1] == 0, bytes[index + 2] == 0, bytes[index + 3] == 1 {
            types.append(bytes[index + 4] & 0x1F)
            index += 4
        } else {
            index += 1
        }
    }
    return types
}
