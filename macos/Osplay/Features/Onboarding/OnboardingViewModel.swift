import AppKit
import Foundation
import Observation

/// The permissions Osplay needs, with their live status (DEVPLAN Phase 11 onboarding).
@MainActor
@Observable
final class OnboardingViewModel {
    private(set) var screenRecording = false
    private(set) var localNetwork: LocalNetworkAccess = .unknown

    private let permissions: any PermissionChecking
    private let localNetworkCheck: any LocalNetworkChecking
    private let settings: AppSettings
    private var tasks: [Task<Void, Never>] = []

    init(permissions: any PermissionChecking, localNetworkCheck: any LocalNetworkChecking, settings: AppSettings) {
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
    }
}
