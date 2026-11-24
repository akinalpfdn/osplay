import Foundation

/// Receives the captured mix: interleaved stereo float PCM at 48 kHz (`Tunables.Audio`), and the
/// first sample frame's capture time on the core clock. Runs on the capture's real-time queue;
/// the samples are valid only during the call.
typealias SystemAudioOutput = @Sendable (UnsafeBufferPointer<Float>, _ captureTimeMicroseconds: UInt64) -> Void

enum AudioCaptureError: Error, Equatable, Sendable {
    /// This macOS has no process taps (before 14.2).
    case unavailable
    /// A Core Audio call failed with this status.
    case failed(status: Int32)
}

/// Captures the Mac's whole sound output and mutes it on the Mac while capturing (AUD-02).
protocol SystemAudioCapturing: AnyObject, Sendable {
    func start() throws(AudioCaptureError)
    /// Stops capturing; the Mac plays its sound again.
    func stop()
}

/// Creates captures; injected so the host session can be tested without Core Audio.
protocol SystemAudioCaptureMaking: Sendable {
    /// False before macOS 14.2.
    var isAvailable: Bool { get }
    func makeCapture(output: @escaping SystemAudioOutput) -> any SystemAudioCapturing
}

/// The system audio recording permission (TCC's AudioCapture service), which the first capture
/// asks for. Separate from `PermissionChecking`: sound is optional, the rest of hosting is not.
protocol AudioCapturePermissionChecking: Sendable {
    var access: AudioCaptureAccess { get }
    /// Shows the system prompt if the user was never asked; true when access is granted.
    func request() async -> Bool
    /// The Screen & System Audio Recording pane of System Settings.
    var settingsURL: URL? { get }
}

enum AudioCaptureAccess: Sendable, Equatable {
    case granted
    case denied
    /// Never asked, or macOS does not say.
    case unknown
}
