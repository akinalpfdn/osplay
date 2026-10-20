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
    private let showPermissions: @MainActor () -> Void

    init(
        settings: AppSettings, connection: ConnectionViewModel, launchAtLogin: any LaunchAtLoginManaging,
        hotkeys: any HotkeyRegistering, logs: any LogExporting, showPermissions: @escaping @MainActor () -> Void
    ) {
        self.showPermissions = showPermissions
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
        take(hotkey, current: settings.viewerHotkey, other: settings.swapHotkey) { settings.viewerHotkey = $0 }
    }

    func resetHotkey() {
        settings.viewerHotkey = Tunables.Viewer.toggleHotkey
        message = nil
    }

    func recordSwapHotkey(_ hotkey: Hotkey) {
        take(hotkey, current: settings.swapHotkey, other: settings.viewerHotkey) { settings.swapHotkey = $0 }
    }

    func resetSwapHotkey() {
        settings.swapHotkey = Tunables.Viewer.swapHotkey
        message = nil
    }

    private func take(_ hotkey: Hotkey, current: Hotkey, other: Hotkey, store: (Hotkey) -> Void) {
        guard hotkey != current else { return }
        guard hotkey != other else {
            message = String(localized: "settings.hotkey.duplicate \(hotkey.displayText)")
            return
        }
        do throws(HotkeyError) {
            let probe = try hotkeys.register(hotkey) {}
            hotkeys.unregister(probe)
            store(hotkey)
            message = nil
        } catch {
            message = String(localized: "settings.hotkey.taken \(hotkey.displayText)")
        }
    }

    func openPermissions() { showPermissions() }

    // MARK: Displays and devices

    func hiDPIChoice(for peer: TrustedPeer) -> HiDPIChoice {
        settings.hiDPIChoice(for: DeviceIdentity.deviceID(publicKey: peer.publicKey))
    }

    func setHiDPIChoice(_ choice: HiDPIChoice, for peer: TrustedPeer) {
        settings.setHiDPIChoice(choice, for: DeviceIdentity.deviceID(publicKey: peer.publicKey))
    }

    /// This Mac's main display, at the origin of every arrangement.
    var mainDisplaySize: CGSize { DisplayArranger.mainDisplaySize }

    /// Known once the device's displays were shown at least once.
    func arrangement(for peer: TrustedPeer) -> [ArrangedMonitor] {
        settings.arrangement(for: DeviceIdentity.deviceID(publicKey: peer.publicKey))
    }

    /// Drops a monitor at `origin`: it snaps to an edge without overlapping, is saved, and moves
    /// at once when that device is connected.
    func move(_ monitorID: UInt8, to origin: CGPoint, for peer: TrustedPeer) {
        let deviceID = DeviceIdentity.deviceID(publicKey: peer.publicKey)
        var arrangement = settings.arrangement(for: deviceID)
        guard let index = arrangement.firstIndex(where: { $0.id == monitorID }) else { return }
        let others = arrangement.filter { $0.id != monitorID }.map(\.frame)
        let main = CGRect(origin: .zero, size: mainDisplaySize)
        let dropped = CGRect(origin: origin, size: arrangement[index].size)
        arrangement[index].origin = DisplayLayout.snap(dropped, among: [main] + others).origin
        settings.setArrangement(arrangement, for: deviceID)
        connection.applyArrangement(arrangement, for: deviceID)
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
