import AppKit
import CoreBridge
import SwiftUI

/// The menu bar window (DEVPLAN Phase 11): role, nearby computers, the current session, quality,
/// and the way to Settings.
struct MenuView: View {
    let connection: ConnectionViewModel
    @Bindable var settings: AppSettings
    let openOnboarding: () -> Void
    #if DEBUG
        let developer: DeveloperTools
    #endif

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.large) {
            MenuHeader(status: connection.status)
            RolePicker(connection: connection)

            switch connection.mode {
            case .off:
                EmptyView()
            case .sharing:
                SharingSection(connection: connection, quality: $settings.qualityPreset)
            case .viewing:
                ViewingSection(connection: connection, hotkey: settings.viewerHotkey)
            }

            if let error = connection.error {
                ErrorBanner(error: error, connection: connection)
            }

            Divider()
            MenuFooter(openOnboarding: openOnboarding)

            #if DEBUG
                DisclosureGroup("developer.tools") {
                    developer.view
                }
                .font(Theme.Typography.caption)
            #endif
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Size.menuWidth, alignment: .leading)
    }
}

#if DEBUG
    /// The debug-only developer sections, built by the app.
    struct DeveloperTools {
        let view: AnyView
    }
#endif

private struct MenuHeader: View {
    let status: ConnectionViewModel.Status

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Text("app.name")
                .font(Theme.Typography.title)
            Text(statusText)
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
        }
        .accessibilityElement(children: .combine)
    }

    private var statusText: LocalizedStringKey {
        switch status {
        case .idle: "menu.status.idle"
        case .waitingForViewer: "connection.status.waiting"
        case .connecting(let peer): "connection.status.connecting \(peer)"
        case .connected(let peer): "connection.status.connected \(peer)"
        }
    }
}

private struct RolePicker: View {
    let connection: ConnectionViewModel

    var body: some View {
        Picker("connection.mode", selection: Binding(get: { connection.mode }, set: { connection.setMode($0) })) {
            Text("connection.mode.off").tag(ConnectionViewModel.Mode.off)
            Text("connection.mode.sharing").tag(ConnectionViewModel.Mode.sharing)
            Text("connection.mode.viewing").tag(ConnectionViewModel.Mode.viewing)
        }
        .pickerStyle(.segmented)
        .labelsHidden()
        .accessibilityLabel(Text("connection.mode"))
    }
}

private struct SharingSection: View {
    let connection: ConnectionViewModel
    @Binding var quality: QualityPreset

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Card {
                HStack(spacing: Theme.Spacing.medium) {
                    Image(systemName: Theme.Symbol.sharing)
                        .font(Theme.Typography.symbol)
                        .foregroundStyle(connection.isSessionActive ? Theme.Colors.accent : Theme.Colors.secondary)
                        .frame(width: Theme.Size.symbolColumn)
                        .accessibilityHidden(true)
                    Text(connection.isSessionActive ? "menu.sharing.active" : "menu.sharing.waiting")
                        .font(Theme.Typography.rowTitle)
                    Spacer()
                    if connection.isSessionActive {
                        Button("connection.disconnect") { connection.disconnect() }
                    }
                }
            }
            Picker("menu.quality", selection: $quality) {
                ForEach(QualityPreset.allCases) { preset in
                    Text(preset.title).tag(preset)
                }
            }
            .pickerStyle(.menu)
            Text("menu.quality.hint")
                .font(Theme.Typography.caption)
                .foregroundStyle(Theme.Colors.secondary)
        }
    }
}

private struct ViewingSection: View {
    let connection: ConnectionViewModel
    let hotkey: Hotkey

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Text("menu.nearby")
                .font(Theme.Typography.sectionTitle)
                .foregroundStyle(Theme.Colors.secondary)
            if connection.devices.isEmpty {
                Text("connection.searching")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            ForEach(connection.devices) { device in
                DeviceRow(
                    name: connection.displayName(of: device), platform: device.advertisement.platform,
                    isPaired: connection.isPaired(device), isBusy: connection.isSessionActive
                ) { connection.connect(to: device) }
            }
            if connection.isSessionActive {
                HStack {
                    Text("menu.hotkeyHint \(hotkey.displayText)")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.secondary)
                    Spacer()
                    Button("connection.disconnect") { connection.disconnect() }
                }
            }
        }
    }
}

private struct DeviceRow: View {
    let name: String
    let platform: PeerPlatform
    let isPaired: Bool
    let isBusy: Bool
    let connect: () -> Void

    var body: some View {
        Card {
            HStack(spacing: Theme.Spacing.medium) {
                Image(systemName: platform == .windows ? Theme.Symbol.windows : Theme.Symbol.mac)
                    .font(Theme.Typography.symbol)
                    .frame(width: Theme.Size.symbolColumn)
                    .accessibilityHidden(true)
                VStack(alignment: .leading) {
                    Text(verbatim: name)
                        .font(Theme.Typography.rowTitle)
                    Text(isPaired ? "menu.device.paired" : "menu.device.new")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.secondary)
                }
                Spacer()
                Button("connection.connect", action: connect)
                    .disabled(isBusy)
            }
        }
        .accessibilityElement(children: .combine)
    }
}

private struct ErrorBanner: View {
    let error: ConnectionError
    let connection: ConnectionViewModel

    var body: some View {
        HStack(alignment: .top, spacing: Theme.Spacing.medium) {
            Image(systemName: Theme.Symbol.warning)
                .foregroundStyle(Theme.Colors.warning)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text(error.message)
                    .font(Theme.Typography.caption)
                    .fixedSize(horizontal: false, vertical: true)
                if error.needsScreenRecordingPermission {
                    Button("permission.screenRecording.open") { connection.openScreenRecordingSettings() }
                }
                if error.needsLocalNetworkPermission {
                    Button("permission.localNetwork.open") { connection.openLocalNetworkSettings() }
                }
            }
        }
        .accessibilityElement(children: .combine)
    }
}

private struct MenuFooter: View {
    let openOnboarding: () -> Void

    var body: some View {
        HStack {
            SettingsLink {
                Label("menu.settings", systemImage: Theme.Symbol.settings)
            }
            .simultaneousGesture(TapGesture().onEnded { NSApp.activate() })
            Button("menu.permissions", action: openOnboarding)
            Spacer()
            Button {
                NSApplication.shared.terminate(nil)
            } label: {
                Label("menu.quit", systemImage: Theme.Symbol.quit)
            }
            .keyboardShortcut("q")
        }
        .labelStyle(.titleAndIcon)
        .buttonStyle(.borderless)
    }
}

/// A grouped module, like Control Center's.
struct Card<Content: View>: View {
    @ViewBuilder let content: Content

    var body: some View {
        content
            .padding(Theme.Spacing.medium)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.Colors.card, in: RoundedRectangle(cornerRadius: Theme.Radius.card))
    }
}

extension QualityPreset {
    var title: LocalizedStringKey {
        switch self {
        case .balanced: "quality.balanced"
        case .high: "quality.high"
        case .ultra: "quality.ultra"
        }
    }
}
