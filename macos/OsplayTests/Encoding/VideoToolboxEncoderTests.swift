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

    @Test("The stream references one frame, so a decoder holds no frames back")
    func referencesOneFrame() throws {
        let collector = FrameCollector()
        let encoder = try VideoToolboxEncoder(configuration) { collector.append($0) }
        let buffer = try makeFrame(width: 640, height: 360, shade: 0)
        try encoder.encode(buffer, presentationTime: .zero, forceKeyframe: true)
        encoder.flush()
        encoder.invalidate()

        let keyframe = try #require(collector.frames.first).get()
        let sps = try #require(nalUnits(keyframe.data).first { ($0.first ?? 0) & 0x1F == 7 })
        #expect(try maximumReferenceFrames(sps) == Tunables.Encoding.referenceFrames)
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

/// The NAL units of an Annex B buffer, without their start codes.
private func nalUnits(_ data: Data) -> [Data] {
    let bytes = [UInt8](data)
    var starts: [Int] = []
    var index = 0
    while index + 3 <= bytes.count {
        if bytes[index] == 0, bytes[index + 1] == 0, bytes[index + 2] == 1 {
            starts.append(index + 3)
            index += 3
        } else {
            index += 1
        }
    }
    return starts.enumerated().map { offset, start in
        var end = offset + 1 < starts.count ? starts[offset + 1] - 3 : bytes.count
        while end > start, bytes[end - 1] == 0 { end -= 1 }  // the next start code's leading zero
        return Data(bytes[start..<end])
    }
}

private struct SPSParseError: Error {}

/// max_num_ref_frames of a High-profile SPS without scaling matrices (what VideoToolbox writes).
private func maximumReferenceFrames(_ sps: Data) throws -> Int {
    var rbsp: [UInt8] = []
    for byte in sps.dropFirst() {  // the NAL header
        if byte == 3, rbsp.count >= 2, rbsp[rbsp.count - 1] == 0, rbsp[rbsp.count - 2] == 0 { continue }
        rbsp.append(byte)
    }
    var bit = 24  // profile_idc, constraint flags, level_idc
    func read(_ count: Int) throws -> Int {
        var value = 0
        for _ in 0..<count {
            guard bit / 8 < rbsp.count else { throw SPSParseError() }
            value = value << 1 | Int(rbsp[bit / 8] >> (7 - UInt8(bit % 8)) & 1)
            bit += 1
        }
        return value
    }
    func exponentialGolomb() throws -> Int {
        var zeros = 0
        while try read(1) == 0 { zeros += 1 }
        return (1 << zeros) - 1 + (try read(zeros))
    }
    _ = try exponentialGolomb()  // seq_parameter_set_id
    guard try exponentialGolomb() == 1 else { throw SPSParseError() }  // chroma_format_idc 4:2:0
    _ = try exponentialGolomb()  // bit_depth_luma_minus8
    _ = try exponentialGolomb()  // bit_depth_chroma_minus8
    _ = try read(1)  // qpprime_y_zero_transform_bypass_flag
    guard try read(1) == 0 else { throw SPSParseError() }  // seq_scaling_matrix_present_flag
    _ = try exponentialGolomb()  // log2_max_frame_num_minus4
    guard try exponentialGolomb() == 0 else { throw SPSParseError() }  // pic_order_cnt_type
    _ = try exponentialGolomb()  // log2_max_pic_order_cnt_lsb_minus4
    return try exponentialGolomb()  // max_num_ref_frames
}
