import Foundation

/// The AudioCapture permission through TCC's own functions: macOS has no public API that reads it
/// or asks for it without starting a capture (DECISIONS.md, 2026-11-20). When the functions are
/// missing the status is `.unknown` and the first capture shows the prompt.
struct SystemAudioPermission: AudioCapturePermissionChecking {
    private typealias Preflight = @convention(c) (CFString, CFDictionary?) -> Int
    private typealias Request =
        @convention(c) (CFString, CFDictionary?, @convention(block) @escaping (Bool) -> Void)
        -> Void

    private static let service = "kTCCServiceAudioCapture"
    private static let frameworkPath = "/System/Library/PrivateFrameworks/TCC.framework/Versions/A/TCC"

    var access: AudioCaptureAccess {
        guard let symbol = Self.symbol("TCCAccessPreflight") else { return .unknown }
        switch unsafeBitCast(symbol, to: Preflight.self)(Self.service as CFString, nil) {
        case 0: return .granted
        case 1: return .denied
        default: return .unknown
        }
    }

    func request() async -> Bool {
        guard let symbol = Self.symbol("TCCAccessRequest") else { return false }
        let request = unsafeBitCast(symbol, to: Request.self)
        return await withCheckedContinuation { continuation in
            request(Self.service as CFString, nil) { granted in continuation.resume(returning: granted) }
        }
    }

    var settingsURL: URL? {
        URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture")
    }

    /// dlopen hands back the same handle for a loaded framework, so asking each time costs little.
    private static func symbol(_ name: String) -> UnsafeMutableRawPointer? {
        dlopen(frameworkPath, RTLD_NOW).flatMap { dlsym($0, name) }
    }
}
