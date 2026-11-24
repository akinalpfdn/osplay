import AVFoundation
import Accelerate
import AudioToolbox
import CoreAudio
import Foundation
import OSLog

/// Process taps where macOS has them (14.2+).
struct ProcessTapCaptureFactory: SystemAudioCaptureMaking {
    var isAvailable: Bool {
        if #available(macOS 14.2, *) { true } else { false }
    }

    func makeCapture(output: @escaping SystemAudioOutput) -> any SystemAudioCapturing {
        if #available(macOS 14.2, *) {
            ProcessTapCapture(output: output)
        } else {
            UnavailableAudioCapture()
        }
    }
}

private final class UnavailableAudioCapture: SystemAudioCapturing {
    func start() throws(AudioCaptureError) { throw .unavailable }
    func stop() {}
}

/// A global stereo process tap, muted on the Mac while tapped, read through a private aggregate
/// device whose clock is the default output device's. When the default output changes (headphones
/// plugged in), the tap is built again on the new device.
@available(macOS 14.2, *)
final class ProcessTapCapture: SystemAudioCapturing, @unchecked Sendable {
    // @unchecked: every Core Audio object below is created, changed and destroyed with `lock`
    // held; the I/O block touches only its own converter.
    private let output: SystemAudioOutput
    private let lock = NSLock()
    private let queue = DispatchQueue(label: "com.akinalpfdn.osplay.audio-tap", qos: .userInteractive)
    private var tapID = AudioObjectID(kAudioObjectUnknown)
    private var aggregateID = AudioObjectID(kAudioObjectUnknown)
    private var procID: AudioDeviceIOProcID?
    private var running = false
    private var outputListener: AudioObjectPropertyListenerBlock?
    private var volume: OutputVolume?

    init(output: @escaping SystemAudioOutput) {
        self.output = output
    }

    deinit {
        stop()
    }

    func start() throws(AudioCaptureError) {
        lock.lock()
        defer { lock.unlock() }
        guard !running else { return }
        do throws(AudioCaptureError) {
            try build()
        } catch {
            tearDown()
            throw error
        }
        running = true
        watchDefaultOutput()
    }

    func stop() {
        lock.lock()
        defer { lock.unlock() }
        guard running else { return }
        running = false
        unwatchDefaultOutput()
        tearDown()
    }

    private func build() throws(AudioCaptureError) {
        let description = CATapDescription(stereoGlobalTapButExcludeProcesses: [])
        description.uuid = UUID()
        description.name = "Osplay"
        description.isPrivate = true
        description.muteBehavior = .mutedWhenTapped
        try check(AudioHardwareCreateProcessTap(description, &tapID))
        let tapFormat = try Self.tapFormat(tapID)
        let (outputDevice, outputUID) = try Self.defaultOutput()
        let aggregate: [String: Any] = [
            kAudioAggregateDeviceNameKey: "Osplay",
            kAudioAggregateDeviceUIDKey: UUID().uuidString,
            kAudioAggregateDeviceMainSubDeviceKey: outputUID,
            kAudioAggregateDeviceIsPrivateKey: true,
            kAudioAggregateDeviceIsStackedKey: false,
            kAudioAggregateDeviceTapAutoStartKey: true,
            kAudioAggregateDeviceSubDeviceListKey: [[kAudioSubDeviceUIDKey: outputUID]],
            kAudioAggregateDeviceTapListKey: [
                [kAudioSubTapDriftCompensationKey: true, kAudioSubTapUIDKey: description.uuid.uuidString]
            ],
        ]
        try check(AudioHardwareCreateAggregateDevice(aggregate as CFDictionary, &aggregateID))
        let converter = try TapConverter(input: tapFormat)
        let volume = OutputVolume(device: outputDevice, queue: queue)
        self.volume = volume
        let output = output
        try check(
            AudioDeviceCreateIOProcIDWithBlock(&procID, aggregateID, queue) { _, input, inputTime, _, _ in
                converter.convert(input, capturedAt: inputTime.pointee, gain: volume.gain, output: output)
            })
        try check(AudioDeviceStart(aggregateID, procID))
        Log.audio.info(
            "Audio tap on \(outputUID, privacy: .public) at \(tapFormat.mSampleRate, privacy: .public) Hz")
    }

    private func tearDown() {
        volume?.stop()
        volume = nil
        if aggregateID != kAudioObjectUnknown, let procID {
            AudioDeviceStop(aggregateID, procID)
            AudioDeviceDestroyIOProcID(aggregateID, procID)
        }
        procID = nil
        if aggregateID != kAudioObjectUnknown {
            AudioHardwareDestroyAggregateDevice(aggregateID)
            aggregateID = AudioObjectID(kAudioObjectUnknown)
        }
        if tapID != kAudioObjectUnknown {
            AudioHardwareDestroyProcessTap(tapID)
            tapID = AudioObjectID(kAudioObjectUnknown)
        }
    }

    private func watchDefaultOutput() {
        var address = Self.defaultOutputAddress
        let listener: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            self?.followDefaultOutput()
        }
        if AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &address, queue, listener)
            == noErr
        {
            outputListener = listener
        }
    }

    private func unwatchDefaultOutput() {
        guard let listener = outputListener else { return }
        var address = Self.defaultOutputAddress
        AudioObjectRemovePropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &address, queue, listener)
        outputListener = nil
    }

    private func followDefaultOutput() {
        lock.lock()
        defer { lock.unlock() }
        guard running else { return }
        tearDown()
        do throws(AudioCaptureError) {
            try build()
        } catch {
            // The sound stays on the Mac until the choice is made again; nothing else can be done.
            tearDown()
            Log.audio.error("Audio tap failed on the new output: \(String(describing: error), privacy: .public)")
        }
    }

    private static var defaultOutputAddress: AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultSystemOutputDevice, mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
    }

    private static func tapFormat(_ tapID: AudioObjectID) throws(AudioCaptureError) -> AudioStreamBasicDescription {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioTapPropertyFormat, mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
        var format = AudioStreamBasicDescription()
        var size = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
        try check(AudioObjectGetPropertyData(tapID, &address, 0, nil, &size, &format))
        return format
    }

    private static func defaultOutput() throws(AudioCaptureError) -> (device: AudioObjectID, uid: String) {
        var address = defaultOutputAddress
        var device = AudioObjectID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        try check(AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &device))
        address.mSelector = kAudioDevicePropertyDeviceUID
        var uid: Unmanaged<CFString>?
        size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        try check(AudioObjectGetPropertyData(device, &address, 0, nil, &size, &uid))
        guard let uid else { throw .failed(status: kAudioHardwareUnspecifiedError) }
        return (device, uid.takeRetainedValue() as String)
    }
}

private func check(_ status: OSStatus) throws(AudioCaptureError) {
    guard status == noErr else { throw .failed(status: status) }
}

/// The tap's buffer, handed to the converter once. Newer SDKs mark the converter's input block
/// `@Sendable`; it runs synchronously inside `convert`, on the tap's queue.
private final class PendingInput: @unchecked Sendable {
    private var buffer: AVAudioPCMBuffer?

    init(_ buffer: AVAudioPCMBuffer) {
        self.buffer = buffer
    }

    func take() -> AVAudioPCMBuffer? {
        defer { buffer = nil }
        return buffer
    }
}

/// Converts the tap's format (the output device's rate, often non-interleaved) to the protocol's
/// interleaved 48 kHz stereo, and host times to the core clock.
private final class TapConverter: @unchecked Sendable {
    // @unchecked: used only by the tap's I/O block, which Core Audio calls on one queue.
    private let inputFormat: AVAudioFormat
    private let converter: AVAudioConverter
    private let outputBuffer: AVAudioPCMBuffer

    init(input description: AudioStreamBasicDescription) throws(AudioCaptureError) {
        var description = description
        guard let inputFormat = AVAudioFormat(streamDescription: &description),
            let outputFormat = AVAudioFormat(
                commonFormat: .pcmFormatFloat32, sampleRate: Double(Tunables.Audio.sampleRate),
                channels: AVAudioChannelCount(Tunables.Audio.channels), interleaved: true),
            let converter = AVAudioConverter(from: inputFormat, to: outputFormat),
            let outputBuffer = AVAudioPCMBuffer(
                pcmFormat: outputFormat, frameCapacity: Tunables.Audio.conversionCapacityFrames)
        else { throw .failed(status: kAudioHardwareUnsupportedOperationError) }
        self.inputFormat = inputFormat
        self.converter = converter
        self.outputBuffer = outputBuffer
    }

    /// `gain` is the Mac's output volume (OutputVolume), applied after conversion.
    func convert(
        _ list: UnsafePointer<AudioBufferList>, capturedAt time: AudioTimeStamp, gain: Float,
        output: SystemAudioOutput
    ) {
        guard let input = AVAudioPCMBuffer(pcmFormat: inputFormat, bufferListNoCopy: list, deallocator: nil),
            input.frameLength > 0
        else { return }
        outputBuffer.frameLength = 0
        let pending = PendingInput(input)
        var error: NSError?
        let status = converter.convert(to: outputBuffer, error: &error) { _, inputStatus in
            guard let buffer = pending.take() else {
                inputStatus.pointee = .noDataNow
                return nil
            }
            inputStatus.pointee = .haveData
            return buffer
        }
        guard status != .error, outputBuffer.frameLength > 0, let samples = outputBuffer.floatChannelData?[0] else {
            return
        }
        let count = Int(outputBuffer.frameLength) * Tunables.Audio.channels
        if gain != 1 {
            var scale = gain
            vDSP_vsmul(samples, 1, &scale, samples, 1, vDSP_Length(count))
        }
        output(UnsafeBufferPointer(start: samples, count: count), Self.coreMicroseconds(time))
    }

    private static func coreMicroseconds(_ time: AudioTimeStamp) -> UInt64 {
        let now = AudioConvertHostTimeToNanos(AudioGetCurrentHostTime()) / 1000
        guard time.mFlags.contains(.hostTimeValid) else {
            return CaptureClock.coreMicroseconds(fromHostMicroseconds: now, hostNow: now)
        }
        return CaptureClock.coreMicroseconds(
            fromHostMicroseconds: AudioConvertHostTimeToNanos(time.mHostTime) / 1000, hostNow: now)
    }
}
