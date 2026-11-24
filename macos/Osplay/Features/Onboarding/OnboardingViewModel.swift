import AppKit
import Foundation
import Observation

/// The permissions Osplay needs, with their live status (DEVPLAN Phase 11 onboarding).
@MainActor
@Observable
final class OnboardingViewModel {
    private(set) var screenRecording = false
    private(set) var localNetwork: LocalNetworkAccess = .unknown
    /// Optional (AUD-02): only sound on a display needs it, so it does not count for `isComplete`.
    private(set) var systemAudio: AudioCaptureAccess = .unknown
    /// Hidden before macOS 14.2, which cannot send its sound.
    let offersSystemAudio: Bool

    private let permissions: any PermissionChecking
    private let localNetworkCheck: any LocalNetworkChecking
    private let settings: AppSettings
    private let audioPermission: any AudioCapturePermissionChecking
    private var tasks: [Task<Void, Never>] = []

    init(
        permissions: any PermissionChecking, localNetworkCheck: any LocalNetworkChecking, settings: AppSettings,
        audioPermission: any AudioCapturePermissionChecking, offersSystemAudio: Bool
    ) {
        self.audioPermission = audioPermission
        self.offersSystemAudio = offersSystemAudio
        systemAudio = audioPermission.access
        self.permissions = permissions
        self.localNetworkCheck = localNetworkCheck
        self.settings = settings
        screenRecording = permissions.hasScreenRecordingAccess
    }

    /// Everything granted (local network counts once macOS reports it allowed).
    var isComplete: Bool { screenRecording && localNetwork == .allowed }

    /// Whether the window should open at launch.
    var needsAttention: Bool { !settings.hasCompletedOnboarding || !permissions.hasScreenRecordingAccess }

    /// Starts watching; System Settings changes show up within a second.
    func start() {
        stop()
        let updates = localNetworkCheck.accessUpdates()
        tasks = [
            Task { [weak self] in
                for await access in updates {
                    self?.localNetwork = access
                }
            },
            Task { [weak self] in
                while !Task.isCancelled {
                    self?.refreshScreenRecording()
                    // Cancellation ends the loop through the condition above.
                    try? await Task.sleep(for: Tunables.Onboarding.pollInterval)
                }
            },
        ]
    }

    func stop() {
        tasks.forEach { $0.cancel() }
        tasks = []
    }

    func allowScreenRecording() {
        if !permissions.requestScreenRecordingAccess(), let url = permissions.screenRecordingSettingsURL {
            NSWorkspace.shared.open(url)
        }
        refreshScreenRecording()
    }

    /// Asks once; afterwards only System Settings can change it.
    func allowSystemAudio() {
        guard systemAudio != .denied else {
            if let url = audioPermission.settingsURL { NSWorkspace.shared.open(url) }
            return
        }
        Task { [weak self, audioPermission] in
            _ = await audioPermission.request()
            self?.systemAudio = audioPermission.access
        }
    }

    func openLocalNetworkSettings() {
        guard let url = permissions.localNetworkSettingsURL else { return }
        NSWorkspace.shared.open(url)
    }

    func finish() {
        settings.hasCompletedOnboarding = true
        stop()
    }

    private func refreshScreenRecording() {
        screenRecording = permissions.hasScreenRecordingAccess
        systemAudio = audioPermission.access
    }
}
