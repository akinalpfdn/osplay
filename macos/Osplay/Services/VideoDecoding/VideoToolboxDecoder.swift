import CoreBridge
import CoreMedia
import Foundation
import OSLog
import VideoToolbox

/// H.264 decoding with VideoToolbox in realtime mode. The format description comes from the SPS
/// and PPS in front of every keyframe; after any error, frames are dropped until the next keyframe
/// so the viewer never shows a corrupted picture for long.
final class VideoToolboxDecoder: VideoDecoding, @unchecked Sendable {
    // @unchecked: `session`, `format` and `parameterSets` are only touched by `decode` (one
    // serial queue) and `invalidate`; `awaitingKeyframe` and `invalidated` are guarded by `lock`,
    // because VideoToolbox reports errors on its own thread.
    private let output: DecoderOutput
    private let lock = NSLock()
    private var session: VTDecompressionSession?
    private var format: CMVideoFormatDescription?
    private var parameterSets: [Data] = []
    private var awaitingKeyframe = true
    private var invalidated = false

    init(output: @escaping DecoderOutput) {
        self.output = output
    }

    deinit {
        invalidate()
    }

    func decode(_ unit: AccessUnit) {
        guard !lock.withLock({ invalidated }) else { return }
        let units: [Data]
        do {
            units = try AnnexB.nalUnits(in: unit.data)
        } catch {
            fail(.invalidData)
            return
        }
        if unit.isKeyframe {
            let sets = units.filter { [AnnexB.NALType.sps, AnnexB.NALType.pps].contains(AnnexB.type(of: $0)) }
            if sets.count >= 2, sets != parameterSets {
                do throws(VideoDecoderError) {
                    try configure(parameterSets: sets)
                } catch {
                    fail(error)
                    return
                }
            }
        }
        let waiting = lock.withLock {
            if unit.isKeyframe { awaitingKeyframe = false }
            return awaitingKeyframe
        }
        guard !waiting, let session, let format else {
            output(.failure(.keyframeNeeded))
            return
        }
        guard let sample = Self.sampleBuffer(AnnexB.lengthPrefixedSlices(units), format: format, unit: unit) else {
            fail(.invalidData)
            return
        }
        let streamID = unit.streamID
        let captureTime = unit.captureTimeMicroseconds
        let status = VTDecompressionSessionDecodeFrame(
            session, sampleBuffer: sample, flags: [._EnableAsynchronousDecompression], infoFlagsOut: nil
        ) { [weak self] status, flags, imageBuffer, _, _ in
            guard let self, !lock.withLock({ invalidated }) else { return }
            if flags.contains(.frameDropped) { return }
            guard status == noErr, let imageBuffer else {
                fail(.decodeFailed(status))
                return
            }
            output(
                .success(
                    DecodedFrame(streamID: streamID, pixelBuffer: imageBuffer, captureTimeMicroseconds: captureTime))
            )
        }
        if status != noErr {
            fail(.decodeFailed(status))
        }
    }

    func invalidate() {
        let first = lock.withLock {
            defer { invalidated = true }
            return !invalidated
        }
        if first, let session {
            VTDecompressionSessionInvalidate(session)
        }
    }

    private func fail(_ error: VideoDecoderError) {
        lock.withLock { awaitingKeyframe = true }
        Log.decoding.notice("Decoder waits for a keyframe: \(String(describing: error), privacy: .public)")
        output(.failure(error))
    }

    private func configure(parameterSets sets: [Data]) throws(VideoDecoderError) {
        guard let created = Self.formatDescription(sets) else { throw .invalidData }
        parameterSets = sets
        format = created
        if let session, VTDecompressionSessionCanAcceptFormatDescription(session, formatDescription: created) {
            return
        }
        if let session {
            VTDecompressionSessionInvalidate(session)
            self.session = nil
        }
        session = try Self.makeSession(created)
    }

    private static func makeSession(
        _ format: CMVideoFormatDescription
    ) throws(VideoDecoderError)
        -> VTDecompressionSession
    {
        // IOSurface-backed output so the presenter can show it without a copy.
        let attributes =
            [
                kCVPixelBufferIOSurfacePropertiesKey: [:] as CFDictionary,
                kCVPixelBufferMetalCompatibilityKey: kCFBooleanTrue as Any,
            ] as CFDictionary
        var created: VTDecompressionSession?
        let status = VTDecompressionSessionCreate(
            allocator: nil, formatDescription: format, decoderSpecification: nil,
            imageBufferAttributes: attributes, outputCallback: nil, decompressionSessionOut: &created)
        guard status == noErr, let created else { throw .sessionCreationFailed(status) }
        VTSessionSetProperty(created, key: kVTDecompressionPropertyKey_RealTime, value: kCFBooleanTrue)
        return created
    }

    private static func sampleBuffer(
        _ avcc: Data, format: CMVideoFormatDescription, unit: AccessUnit
    ) -> CMSampleBuffer? {
        guard !avcc.isEmpty else { return nil }
        var block: CMBlockBuffer?
        guard
            CMBlockBufferCreateWithMemoryBlock(
                allocator: nil, memoryBlock: nil, blockLength: avcc.count, blockAllocator: nil,
                customBlockSource: nil, offsetToData: 0, dataLength: avcc.count, flags: 0, blockBufferOut: &block)
                == noErr, let block
        else { return nil }
        let copied = avcc.withUnsafeBytes { bytes in
            guard let base = bytes.baseAddress else { return kCMBlockBufferBadPointerParameterErr }
            return CMBlockBufferReplaceDataBytes(
                with: base, blockBuffer: block, offsetIntoDestination: 0, dataLength: avcc.count)
        }
        guard copied == noErr else { return nil }
        var timing = CMSampleTimingInfo(
            duration: .invalid,
            presentationTimeStamp: CMTime(value: CMTimeValue(unit.captureTimeMicroseconds), timescale: 1_000_000),
            decodeTimeStamp: .invalid)
        var size = avcc.count
        var sample: CMSampleBuffer?
        guard
            CMSampleBufferCreateReady(
                allocator: nil, dataBuffer: block, formatDescription: format, sampleCount: 1,
                sampleTimingEntryCount: 1, sampleTimingArray: &timing, sampleSizeEntryCount: 1,
                sampleSizeArray: &size, sampleBufferOut: &sample) == noErr
        else { return nil }
        return sample
    }

    /// A format description from SPS and PPS (4-byte NAL lengths in the samples).
    private static func formatDescription(_ sets: [Data]) -> CMVideoFormatDescription? {
        let copies = sets.map { set -> UnsafeMutablePointer<UInt8> in
            let pointer = UnsafeMutablePointer<UInt8>.allocate(capacity: max(set.count, 1))
            set.copyBytes(to: pointer, count: set.count)
            return pointer
        }
        defer { copies.forEach { $0.deallocate() } }
        let pointers = copies.map { UnsafePointer($0) }
        let sizes = sets.map(\.count)
        var created: CMFormatDescription?
        let status = pointers.withUnsafeBufferPointer { pointerBuffer in
            sizes.withUnsafeBufferPointer { sizeBuffer in
                guard let pointerBase = pointerBuffer.baseAddress, let sizeBase = sizeBuffer.baseAddress else {
                    return kCMFormatDescriptionError_InvalidParameter
                }
                return CMVideoFormatDescriptionCreateFromH264ParameterSets(
                    allocator: nil, parameterSetCount: sets.count, parameterSetPointers: pointerBase,
                    parameterSetSizes: sizeBase, nalUnitHeaderLength: 4, formatDescriptionOut: &created)
            }
        }
        return status == noErr ? created : nil
    }
}

/// Creates VideoToolbox decoders.
struct VideoToolboxDecoderFactory: VideoDecoderMaking {
    func makeDecoder(output: @escaping DecoderOutput) -> any VideoDecoding {
        VideoToolboxDecoder(output: output)
    }
}
