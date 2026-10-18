import CoreBridge
import CoreMedia
import CoreVideo
import Foundation
import Testing

@testable import Osplay

@Suite("VideoToolbox decoder", .serialized)
struct VideoToolboxDecoderTests {
    @Test("A stream from the encoder decodes frame by frame")
    func decodesEncodedStream() async throws {
        let units = try encodedUnits(count: 6, keyframes: [0])
        let results = Collector<Result<DecodedFrame, VideoDecoderError>>()
        let decoder = VideoToolboxDecoder { results.append($0) }
        units.forEach(decoder.decode)
        try await eventually { results.items.count == 6 }

        let frames = try results.items.map { try $0.get() }
        #expect(frames.map(\.captureTimeMicroseconds) == units.map(\.captureTimeMicroseconds))
        #expect(CVPixelBufferGetWidth(frames[0].pixelBuffer) == 320)
        #expect(CVPixelBufferGetIOSurface(frames[0].pixelBuffer) != nil)
        decoder.invalidate()
    }

    @Test("Frames before the first keyframe are dropped with keyframeNeeded")
    func waitsForFirstKeyframe() async throws {
        let units = try encodedUnits(count: 4, keyframes: [0, 2])
        let results = Collector<Result<DecodedFrame, VideoDecoderError>>()
        let decoder = VideoToolboxDecoder { results.append($0) }
        decoder.decode(units[1])
        decoder.decode(units[2])
        decoder.decode(units[3])
        try await eventually { results.items.count == 3 }

        #expect(results.items.first.flatMap { try? $0.get() } == nil)
        #expect(results.items.compactMap { try? $0.get() }.count == 2, "decoding starts at the keyframe")
        decoder.invalidate()
    }

    @Test("After invalid data, frames wait for the next keyframe")
    func recoversAtNextKeyframe() async throws {
        let units = try encodedUnits(count: 5, keyframes: [0, 3])
        let results = Collector<Result<DecodedFrame, VideoDecoderError>>()
        let decoder = VideoToolboxDecoder { results.append($0) }
        decoder.decode(units[0])
        decoder.decode(
            AccessUnit(
                streamID: 0, isKeyframe: false, carriesParameterSets: false, captureTimeMicroseconds: 9,
                data: Data([1, 2, 3])))
        decoder.decode(units[2])  // depends on the corrupted frame: dropped
        decoder.decode(units[3])
        decoder.decode(units[4])
        try await eventually { results.items.count == 5 }

        // Errors are reported at once, decoded frames asynchronously: compare as sets.
        let decoded = Set(results.items.compactMap { try? $0.get() }.map(\.captureTimeMicroseconds))
        #expect(decoded == Set([units[0], units[3], units[4]].map(\.captureTimeMicroseconds)))
        let errors = results.items.compactMap { result -> VideoDecoderError? in
            if case .failure(let error) = result { error } else { nil }
        }
        #expect(errors.sorted { "\($0)" < "\($1)" } == [.invalidData, .keyframeNeeded])
        decoder.invalidate()
    }
}

private final class Collector<T: Sendable>: @unchecked Sendable {
    private let lock = NSLock()
    private var stored: [T] = []
    func append(_ item: T) { lock.withLock { stored.append(item) } }
    var items: [T] { lock.withLock { stored } }
}

/// Encodes `count` 320×180 frames (a changing gradient) with the real encoder.
private func encodedUnits(count: Int, keyframes: Set<Int>) throws -> [AccessUnit] {
    let frames = Collector<Result<EncodedFrame, VideoEncoderError>>()
    let encoder = try VideoToolboxEncoder(
        EncoderConfiguration(width: 320, height: 180, frameRate: 60, bitrateKbps: 2_000, profile: .high)
    ) { frames.append($0) }
    for index in 0..<count {
        try encoder.encode(
            try gradient(width: 320, height: 180, offset: index * 16),
            presentationTime: CMTime(value: CMTimeValue(index), timescale: 60), forceKeyframe: keyframes.contains(index)
        )
    }
    encoder.flush()
    encoder.invalidate()
    return try frames.items.map { try $0.get() }.map { frame in
        AccessUnit(
            streamID: 0, isKeyframe: frame.isKeyframe, carriesParameterSets: frame.isKeyframe,
            captureTimeMicroseconds: frame.captureTimeMicroseconds, data: frame.data)
    }
}

private func gradient(width: Int, height: Int, offset: Int) throws -> CVPixelBuffer {
    var buffer: CVPixelBuffer?
    let attributes = [kCVPixelBufferIOSurfacePropertiesKey: [:]] as CFDictionary
    CVPixelBufferCreate(nil, width, height, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, attributes, &buffer)
    let frame = try #require(buffer)
    CVPixelBufferLockBaseAddress(frame, [])
    defer { CVPixelBufferUnlockBaseAddress(frame, []) }
    let luma = try #require(CVPixelBufferGetBaseAddressOfPlane(frame, 0)).assumingMemoryBound(to: UInt8.self)
    let stride = CVPixelBufferGetBytesPerRowOfPlane(frame, 0)
    for row in 0..<height {
        for column in 0..<width {
            luma[row * stride + column] = UInt8(truncatingIfNeeded: column + row + offset)
        }
    }
    let chroma = try #require(CVPixelBufferGetBaseAddressOfPlane(frame, 1))
    memset(chroma, 128, CVPixelBufferGetBytesPerRowOfPlane(frame, 1) * CVPixelBufferGetHeightOfPlane(frame, 1))
    return frame
}

/// Polls `condition` until it holds; fails the test after 3 s.
private func eventually(_ condition: () -> Bool) async throws {
    let deadline = ContinuousClock.now + .seconds(3)
    while !condition() {
        guard ContinuousClock.now < deadline else {
            Issue.record("condition not met within 3 s")
            return
        }
        try await Task.sleep(for: .milliseconds(5))
    }
}
