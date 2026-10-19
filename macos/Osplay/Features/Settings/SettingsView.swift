import CoreBridge
import SwiftUI

/// The Settings window: one tab per DEVPLAN group.
struct SettingsView: View {
    let viewModel: SettingsViewModel

    var body: some View {
        TabView {
            GeneralSettings(viewModel: viewModel)
                .tabItem { Label("settings.general", systemImage: Theme.Symbol.general) }
            DisplaySettings(viewModel: viewModel)
                .tabItem { Label("settings.displays", systemImage: Theme.Symbol.displays) }
            DeviceSettings(viewModel: viewModel)
                .tabItem { Label("settings.devices", systemImage: Theme.Symbol.devices) }
            AdvancedSettings(viewModel: viewModel, settings: viewModel.settings)
                .tabItem { Label("settings.advanced", systemImage: Theme.Symbol.advanced) }
        }
        .frame(width: Theme.Size.settingsWidth, height: Theme.Size.settingsHeight)
        .onAppear(perform: viewModel.appeared)
    }
}

private struct GeneralSettings: View {
    let viewModel: SettingsViewModel

    var body: some View {
        Form {
            Toggle(
                "settings.launchAtLogin",
                isOn: Binding(get: { viewModel.launchesAtLogin }, set: { viewModel.setLaunchesAtLogin($0) }))
            if viewModel.launchNeedsApproval {
                Text("settings.launchAtLogin.approval")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
            }
            LabeledContent("settings.hotkey") {
                VStack(alignment: .trailing, spacing: Theme.Spacing.small) {
                    HotkeyRecorder(hotkey: viewModel.settings.viewerHotkey, record: viewModel.record)
                    Button("settings.hotkey.reset", action: viewModel.resetHotkey)
                        .buttonStyle(.link)
                }
            }
            Text("settings.hotkey.description")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
            if let message = viewModel.message {
                Text(message)
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.error)
            }
        }
        .formStyle(.grouped)
    }
}

private struct DisplaySettings: View {
    let viewModel: SettingsViewModel

    var body: some View {
        Form {
            Section {
                if viewModel.peers.isEmpty {
                    Text("settings.noDevices")
                        .foregroundStyle(Theme.Colors.secondary)
                }
                ForEach(viewModel.peers) { peer in
                    Picker(
                        selection: Binding(
                            get: { viewModel.hiDPIChoice(for: peer) }, set: { viewModel.setHiDPIChoice($0, for: peer) })
                    ) {
                        Text("settings.hiDPI.automatic").tag(HiDPIChoice.automatic)
                        Text("settings.hiDPI.always").tag(HiDPIChoice.always)
                        Text("settings.hiDPI.never").tag(HiDPIChoice.never)
                    } label: {
                        Text(verbatim: peer.name)
                    }
                }
            } footer: {
                Text("settings.hiDPI.description")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
            }
        }
        .formStyle(.grouped)
    }
}

private struct DeviceSettings: View {
    let viewModel: SettingsViewModel

    var body: some View {
        Form {
            Section {
                if viewModel.peers.isEmpty {
                    Text("settings.noDevices")
                        .foregroundStyle(Theme.Colors.secondary)
                }
                ForEach(viewModel.peers) { peer in
                    PeerRow(peer: peer, viewModel: viewModel)
                }
            } footer: {
                Text("settings.devices.description")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
            }
        }
        .formStyle(.grouped)
    }
}

private struct PeerRow: View {
    let peer: TrustedPeer
    let viewModel: SettingsViewModel
    @State private var name = ""

    var body: some View {
        HStack(spacing: Theme.Spacing.medium) {
            TextField("settings.devices.name", text: $name)
                .labelsHidden()
                .onSubmit { viewModel.rename(peer, to: name) }
                .accessibilityLabel(Text("settings.devices.name"))
            if peer.revoked {
                Text("connection.revoked")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.error)
            } else {
                Text(peer.lastSeen, format: .relative(presentation: .named))
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                Button("connection.revoke") { viewModel.revoke(peer) }
            }
            Button("connection.remove", role: .destructive) { viewModel.remove(peer) }
        }
        .onAppear { name = peer.name }
        .onChange(of: peer.name) { _, newName in name = newName }
    }
}

private struct AdvancedSettings: View {
    let viewModel: SettingsViewModel
    @Bindable var settings: AppSettings

    var body: some View {
        Form {
            Toggle("settings.statsOverlay", isOn: $settings.showsStatsOverlay)
            Text("settings.statsOverlay.description")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
            LabeledContent("settings.logs") {
                HStack {
                    Button("settings.logs.export", action: viewModel.exportLogs)
                    Button("settings.logs.open", action: viewModel.openLogsFolder)
                }
            }
            if let message = viewModel.message {
                Text(message)
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.error)
            }
            Text("menu.coreVersion \(CoreVersion.linked.description)")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
        }
        .formStyle(.grouped)
    }
}
