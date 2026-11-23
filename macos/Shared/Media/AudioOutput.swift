import AVFAudio
import CoreBridge
import Foundation

/// Fills interleaved 32-bit float PCM, `channels` per frame, on the audio output's thread.
typealias AudioFill = @Sendable (_ samples: UnsafeMutableBufferPointer<Float>, _ channels: Int) -> Void

/// Plays the host's sound until stopped.
protocol AudioOutput: AnyObject, Sendable {
    func stop()
}

/// Opens this device's audio output for the host's sound; nil when it cannot be opened.
protocol AudioOutputMaking: Sendable {
    func makeOutput(format: AudioFormat, fill: @escaping AudioFill) -> (any AudioOutput)?
}

struct EngineAudioOutputs: AudioOutputMaking {
    func makeOutput(format: AudioFormat, fill: @escaping AudioFill) -> (any AudioOutput)? {
        EngineAudioOutput(format: format, fill: fill)
    }
}

/// An AVAudioEngine whose source node pulls PCM from the core's jitter buffer on the render thread,
/// on the device's default output; it follows output changes and, on iPad and Apple TV, interruptions.
final class EngineAudioOutput: AudioOutput, @unchecked Sendable {
    private let engine = AVAudioEngine()
    private let source: AVAudioSourceNode
    private let format: AVAudioFormat
    private let scratch: Scratch
    private let lock = NSLock()
    private var observers: [NSObjectProtocol] = []
    private var stopped = false

    /// Interleaved PCM from the core before it is split into the node's channel buffers; only the
    /// render thread touches it once the engine runs.
    private final class Scratch: @unchecked Sendable {
        let samples: UnsafeMutableBufferPointer<Float>
        init(count: Int) {
            samples = .allocate(capacity: count)
            samples.initialize(repeating: 0)
        }
        deinit { samples.deallocate() }
    }

    init?(format audioFormat: AudioFormat, fill: @escaping AudioFill) {
        let channels = audioFormat.channels
        guard channels > 0,
            let format = AVAudioFormat(
                standardFormatWithSampleRate: Double(AudioFormat.sampleRate), channels: AVAudioChannelCount(channels))
        else { return nil }
        self.format = format
        let scratch = Scratch(count: Tunables.Playback.chunkFrames * channels)
        self.scratch = scratch
        source = AVAudioSourceNode(format: format) { _, _, frameCount, audioBufferList -> OSStatus in
            let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
            let total = Int(frameCount)
            var done = 0
            while done < total {
                let frames = min(total - done, Tunables.Playback.chunkFrames)
                let chunk = UnsafeMutableBufferPointer(rebasing: scratch.samples[0..<frames * channels])
                fill(chunk, channels)
                for (channel, buffer) in buffers.enumerated() where channel < channels {
                    guard let out = buffer.mData?.assumingMemoryBound(to: Float.self) else { continue }
                    for frame in 0..<frames {
                        out[done + frame] = chunk[frame * channels + channel]
                    }
                }
                done += frames
            }
            return noErr
        }
        engine.attach(source)
        guard start() else {
            Log.viewer.error("The audio output could not start")
            return nil
        }
        observe()
    }

    deinit {
        stop()
    }

    func stop() {
        let first = lock.withLock {
            defer { stopped = true }
            return !stopped
        }
        guard first else { return }
        observers.forEach(NotificationCenter.default.removeObserver)
        engine.stop()  // returns once the render thread is out of the source node
        #if !os(macOS)
            // Deactivating only lowers the session for others; a failure leaves nothing to undo.
            try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
        #endif
    }

    /// Connects the source to the output and starts; also after the output changed, when the
    /// engine has stopped itself and may have dropped its connections.
    @discardableResult
    private func start() -> Bool {
        do {
            #if !os(macOS)
                let session = AVAudioSession.sharedInstance()
                try session.setCategory(.playback, mode: .default)
                try session.setPreferredIOBufferDuration(Tunables.Playback.preferredBufferDuration)
                try session.setActive(true)
            #endif
            engine.connect(source, to: engine.mainMixerNode, format: format)
            engine.prepare()
            try engine.start()
            return true
        } catch {
            Log.viewer.error("Audio output: \(error.localizedDescription, privacy: .public)")
            return false
        }
    }

    private func observe() {
        let center = NotificationCenter.default
        let restart: @Sendable (Notification) -> Void = { [weak self] _ in self?.restart() }
        observers.append(
            center.addObserver(forName: .AVAudioEngineConfigurationChange, object: engine, queue: nil, using: restart))
        #if !os(macOS)
            observers.append(
                center.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: nil) {
                    [weak self] note in
                    let type = (note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt)
                        .flatMap(AVAudioSession.InterruptionType.init)
                    if type == .ended { self?.restart() }
                })
        #endif
    }

    private func restart() {
        lock.withLock {
            guard !stopped, !engine.isRunning else { return }
            if start() {
                Log.viewer.info("Audio output restarted on the current output device")
            }
        }
    }
}
