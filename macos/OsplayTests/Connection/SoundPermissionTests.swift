import Foundation
import Testing

@testable import Osplay

/// A permission whose answer to the prompt the test decides.
private final class PromptedPermission: AudioCapturePermissionChecking, @unchecked Sendable {
    private let lock = NSLock()
    private var current: AudioCaptureAccess
    private let answer: AudioCaptureAccess
    private(set) var prompts = 0

    init(_ access: AudioCaptureAccess, answer: AudioCaptureAccess) {
        current = access
        self.answer = answer
    }

    var access: AudioCaptureAccess { lock.withLock { current } }

    func request() async -> Bool {
        lock.withLock {
            prompts += 1
            current = answer
            return answer == .granted
        }
    }

    var settingsURL: URL? { nil }
}

@MainActor
@Suite("Sound permission")
struct SoundPermissionTests {
    @Test("Granted plays at once, denied never prompts")
    func knownAnswers() async {
        let granted = PromptedPermission(.granted, answer: .granted)
        let denied = PromptedPermission(.denied, answer: .denied)
        #expect(await ConnectionViewModel.audioAllowed(granted))
        #expect(await !ConnectionViewModel.audioAllowed(denied))
        #expect(granted.prompts == 0 && denied.prompts == 0)
    }

    @Test("Never asked: the user's answer to the prompt decides")
    func prompts() async {
        let yes = PromptedPermission(.unknown, answer: .granted)
        let no = PromptedPermission(.unknown, answer: .denied)
        #expect(await ConnectionViewModel.audioAllowed(yes))
        #expect(await !ConnectionViewModel.audioAllowed(no))
        #expect(yes.prompts == 1 && no.prompts == 1)
    }

    @Test("When macOS does not say, sound starts and the capture itself asks")
    func unknownStaysUnknown() async {
        let silent = PromptedPermission(.unknown, answer: .unknown)
        #expect(await ConnectionViewModel.audioAllowed(silent))
    }
}
