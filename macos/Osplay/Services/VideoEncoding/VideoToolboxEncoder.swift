import CoreBridge
import CoreMedia
import Foundation
import OSLog
import VideoToolbox

/// H.264 encoding with VideoToolbox's low-latency rate control: realtime, no frame reordering,
/// keyframes only on request, Annex B output with in-band parameter sets.
final class VideoToolboxEncoder: VideoEncoding, @unchecked Sendable {
    // @unchecked: `session` is set once in init; VTCompressionSession is thread-safe, and
    // `invalidated` is guarded by `lock`.
    private let session: VTCompressionSession
    private let output: EncoderOutput
    private let lock = NSLock()
    private var invalidated = false

    init(_ configuration: EncoderConfiguration, output: @escaping EncoderOutput) throws(VideoEncoderError) {
        self.output = output
        session = try Self.makeSession(configuration)
        try Self.configure(session, configuration)
        let status = VTCompressionSessionPrepareToEncodeFrames(session)
        guard status == noErr else {
            VTCompressionSessionInvalidate(session)
            throw .configurationRejected(status)
        }
    }

    deinit {
        invalidate()
    }

    func encode(_ pixelBuffer: CVPixelBuffer, presentationTime: CMTime, forceKeyframe: Bool) throws(VideoEncoderError) {
        guard !isInvalidated else { throw .invalidated }
        let properties: CFDictionary? =
            forceKeyframe ? [kVTEncodeFrameOptionKey_ForceKeyFrame: kCFBooleanTrue] as CFDictionary : nil
        let output = output
        let status = VTCompressionSessionEncodeFrame(
            session, imageBuffer: pixelBuffer, presentationTimeStamp: presentationTime, duration: .invalid,
            frameProperties: properties, infoFlagsOut: nil
        ) { [weak self] status, flags, sampleBuffer in
            guard let self, !self.isInvalidated else { return }
            if flags.contains(.frameDropped) { return }
            guard status == noErr else {
                output(.failure(.encodeFailed(status)))
                return
            }
            guard let sampleBuffer else { return }
            output(Self.annexBFrame(from: sampleBuffer))
        }
        guard status == noErr else { throw .encodeFailed(status) }
    }

    func setBitrate(kbps: Int) {
        guard !isInvalidated else { return }
        let bitsPerSecond = kbps * 1000
        let average = VTSessionSetProperty(
            session, key: kVTCompressionPropertyKey_AverageBitRate, value: bitsPerSecond as CFNumber)
        if average != noErr {
            Log.encoding.error("Encoder rejected bitrate \(kbps) kbps: \(average)")
        }
        // Not every encoder supports a data rate limit; the average alone still applies.
        let limits =
            [Double(bitsPerSecond) * Tunables.Encoding.peakBitrateFactor / 8, 1.0] as CFArray
        _ = VTSessionSetProperty(session, key: kVTCompressionPropertyKey_DataRateLimits, value: limits)
    }

    func flush() {
        guard !isInvalidated else { return }
        VTCompressionSessionCompleteFrames(session, untilPresentationTimeStamp: .invalid)
    }

    func invalidate() {
        let first = lock.withLock {
            defer { invalidated = true }
            return !invalidated
        }
        if first {
            VTCompressionSessionInvalidate(session)
        }
    }

    private var isInvalidated: Bool { lock.withLock { invalidated } }

    // MARK: - Setup

    private static func makeSession(
        _ configuration: EncoderConfiguration
    ) throws(VideoEncoderError)
        -> VTCompressionSession
    {
        let lowLatency = [kVTVideoEncoderSpecification_EnableLowLatencyRateControl: kCFBooleanTrue] as CFDictionary
        var created: VTCompressionSession?
        var status = create(configuration, specification: lowLatency, &created)
        if status != noErr {
            // Encoders without low-latency rate control (e.g. in virtual machines) still work in
            // realtime mode, with somewhat larger rate-control bursts.
            Log.encoding.notice("Low-latency rate control unavailable (\(status)); using realtime mode")
            status = create(configuration, specification: nil, &created)
        }
        guard status == noErr, let created else { throw .creationFailed(status) }
        return created
    }

    private static func create(
        _ configuration: EncoderConfiguration, specification: CFDictionary?, _ session: inout VTCompressionSession?
    ) -> OSStatus {
        VTCompressionSessionCreate(
            allocator: nil, width: Int32(configuration.width), height: Int32(configuration.height),
            codecType: kCMVideoCodecType_H264, encoderSpecification: specification, imageBufferAttributes: nil,
            compressedDataAllocator: nil, outputCallback: nil, refcon: nil, compressionSessionOut: &session)
    }

    private static func configure(
        _ session: VTCompressionSession, _ configuration: EncoderConfiguration
    )
        throws(VideoEncoderError)
    {
        let tunables = Tunables.Encoding.self
        let bitsPerSecond = configuration.bitrateKbps * 1000
        let required: [(CFString, CFTypeRef)] = [
            (kVTCompressionPropertyKey_RealTime, kCFBooleanTrue),
            (kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse),
            (kVTCompressionPropertyKey_ProfileLevel, profileLevel(configuration.profile)),
            (kVTCompressionPropertyKey_AverageBitRate, bitsPerSecond as CFNumber),
        ]
        for (key, value) in required {
            let status = VTSessionSetProperty(session, key: key, value: value)
            guard status == noErr else {
                VTCompressionSessionInvalidate(session)
                Log.encoding.error("Encoder rejected \(key as String, privacy: .public): \(status)")
                throw .configurationRejected(status)
            }
        }
        // Not every encoder supports these; they improve latency or bound bursts when available.
        let optional: [(CFString, CFTypeRef)] = [
            (kVTCompressionPropertyKey_ExpectedFrameRate, configuration.frameRate as CFNumber),
            // Keyframes come on request (stream start, loss); a periodic one is only a safety net.
            (kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, tunables.maximumKeyframeInterval as CFNumber),
            // Signalled in the stream so the viewer decodes ScreenCaptureKit's BT.709 colors correctly.
            (kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2),
            (kVTCompressionPropertyKey_TransferFunction, kCVImageBufferTransferFunction_ITU_R_709_2),
            (kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2),
            (kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality, kCFBooleanTrue),
            (
                kVTCompressionPropertyKey_DataRateLimits,
                [Double(bitsPerSecond) * tunables.peakBitrateFactor / 8, 1.0] as CFArray
            ),
        ]
        for (key, value) in optional {
            let status = VTSessionSetProperty(session, key: key, value: value)
            if status != noErr {
                Log.encoding.info("Encoder ignores \(key as String, privacy: .public): \(status)")
            }
        }
    }

    private static func profileLevel(_ profile: H264Profile) -> CFString {
        switch profile {
        case .baseline: kVTProfileLevel_H264_ConstrainedBaseline_AutoLevel
        case .main: kVTProfileLevel_H264_Main_AutoLevel
        case .high: kVTProfileLevel_H264_High_AutoLevel
        }
    }

    // MARK: - Output

    /// Annex B with SPS and PPS in front of every keyframe.
    static func annexBFrame(from sampleBuffer: CMSampleBuffer) -> Result<EncodedFrame, VideoEncoderError> {
        guard let dataBuffer = sampleBuffer.dataBuffer, let format = sampleBuffer.formatDescription,
            let layout = parameterSets(of: format)
        else { return .failure(.invalidOutput) }
        let isKeyframe = !sampleBuffer.isNotSync
        do {
            let data = try dataBuffer.withContiguousStorage { bytes in
                try AnnexB.fromLengthPrefixed(
                    bytes, lengthSize: layout.lengthSize, parameterSets: isKeyframe ? layout.sets : [])
            }
            let seconds = sampleBuffer.presentationTimeStamp.seconds
            return .success(
                EncodedFrame(
                    data: data, isKeyframe: isKeyframe,
                    captureTimeMicroseconds: seconds.isFinite ? UInt64(max(seconds, 0) * 1_000_000) : 0))
        } catch {
            return .failure(.invalidOutput)
        }
    }

    private static func parameterSets(of format: CMFormatDescription) -> (sets: [Data], lengthSize: Int)? {
        var count = 0
        var lengthSize: Int32 = 0
        guard
            CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                format, parameterSetIndex: 0, parameterSetPointerOut: nil, parameterSetSizeOut: nil,
                parameterSetCountOut: &count, nalUnitHeaderLengthOut: &lengthSize) == noErr
        else { return nil }
        var sets: [Data] = []
        for index in 0..<count {
            var pointer: UnsafePointer<UInt8>?
            var size = 0
            guard
                CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                    format, parameterSetIndex: index, parameterSetPointerOut: &pointer, parameterSetSizeOut: &size,
                    parameterSetCountOut: nil, nalUnitHeaderLengthOut: nil) == noErr,
                let pointer
            else { return nil }
            sets.append(Data(bytes: pointer, count: size))
        }
        return (sets, Int(lengthSize))
    }
}

private extension CMSampleBuffer {
    /// VideoToolbox marks every frame except keyframes with NotSync.
    var isNotSync: Bool {
        guard let attachments = sampleAttachments.first else { return false }
        return (attachments[.notSync] as? Bool) ?? false
    }
}

/// Creates VideoToolbox encoders.
struct VideoToolboxEncoderFactory: VideoEncoderMaking {
    func makeEncoder(
        _ configuration: EncoderConfiguration, output: @escaping EncoderOutput
    ) throws(VideoEncoderError)
        -> any VideoEncoding
    {
        try VideoToolboxEncoder(configuration, output: output)
    }
}
