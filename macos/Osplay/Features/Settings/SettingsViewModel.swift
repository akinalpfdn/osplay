import AppKit
import CoreBridge
import Foundation
import Observation

/// Settings window (DEVPLAN Phase 11): General, Displays, Devices, Advanced.
@MainActor
@Observable
final class SettingsViewModel {
    let settings: AppSettings
    private(set) var launchesAtLogin: Bool
    private(set) var launchNeedsApproval: Bool
    /// The last problem with a setting, shown under it.
    private(set) var message: String?
    private(set) var exportedLog: URL?

    private let connection: ConnectionViewModel
    private let launchAtLogin: any LaunchAtLoginManaging
    private let hotkeys: any HotkeyRegistering
    private let logs: any LogExporting

    init(
        settings: AppSettings, connection: ConnectionViewModel, launchAtLogin: any LaunchAtLoginManaging,
        hotkeys: any HotkeyRegistering, logs: any LogExporting
    ) {
        self.settings = settings
        self.connection = connection
        self.launchAtLogin = launchAtLogin
        self.hotkeys = hotkeys
        self.logs = logs
        launchesAtLogin = launchAtLogin.isEnabled
        launchNeedsApproval = launchAtLogin.needsApproval
    }

    var peers: [TrustedPeer] { connection.peers }

    func appeared() {
        connection.loadPeers()
        launchesAtLogin = launchAtLogin.isEnabled
        launchNeedsApproval = launchAtLogin.needsApproval
    }

    // MARK: General

    func setLaunchesAtLogin(_ enabled: Bool) {
        do throws(LaunchAtLoginError) {
            try launchAtLogin.setEnabled(enabled)
            message = nil
        } catch {
            message = String(localized: "settings.launchAtLogin.failed")
        }
        launchesAtLogin = launchAtLogin.isEnabled
        launchNeedsApproval = launchAtLogin.needsApproval
    }

    /// Takes the new combination when no other app owns it; it applies from the next connection.
    func record(_ hotkey: Hotkey) {
        guard hotkey != settings.viewerHotkey else { return }
        do throws(HotkeyError) {
            let probe = try hotkeys.register(hotkey) {}
            hotkeys.unregister(probe)
            settings.viewerHotkey = hotkey
            message = nil
        } catch {
            message = String(localized: "settings.hotkey.taken \(hotkey.displayText)")
        }
    }

    func resetHotkey() {
        settings.viewerHotkey = Tunables.Viewer.toggleHotkey
        message = nil
    }

    // MARK: Displays and devices

    func hiDPIChoice(for peer: TrustedPeer) -> HiDPIChoice {
        settings.hiDPIChoice(for: DeviceIdentity.deviceID(publicKey: peer.publicKey))
    }

    func setHiDPIChoice(_ choice: HiDPIChoice, for peer: TrustedPeer) {
        settings.setHiDPIChoice(choice, for: DeviceIdentity.deviceID(publicKey: peer.publicKey))
    }

    func rename(_ peer: TrustedPeer, to name: String) { connection.rename(peer, to: name) }
    func revoke(_ peer: TrustedPeer) { connection.revoke(peer) }
    func remove(_ peer: TrustedPeer) { connection.remove(peer) }

    // MARK: Advanced

    func exportLogs() {
        Task {
            do throws(LogExportError) {
                let file = try await logs.export()
                exportedLog = file
                message = nil
                NSWorkspace.shared.activateFileViewerSelecting([file])
            } catch {
                message = String(localized: "settings.logs.failed")
            }
        }
    }

    func openLogsFolder() {
        // Created so the folder opens even before the first export; a failure shows Finder's error.
        try? FileManager.default.createDirectory(at: logs.folder, withIntermediateDirectories: true)
        NSWorkspace.shared.open(logs.folder)
    }
}
