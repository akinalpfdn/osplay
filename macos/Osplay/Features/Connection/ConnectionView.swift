import CoreBridge
import SwiftUI

/// Temporary "Devices" section of the menu bar window (Phase 10).
struct ConnectionView: View {
    let viewModel: ConnectionViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Text("connection.title")
                .font(Theme.Typography.sectionTitle)

            Picker("connection.mode", selection: Binding(get: { viewModel.mode }, set: { viewModel.setMode($0) })) {
                Text("connection.mode.off").tag(ConnectionViewModel.Mode.off)
                Text("connection.mode.sharing").tag(ConnectionViewModel.Mode.sharing)
                Text("connection.mode.viewing").tag(ConnectionViewModel.Mode.viewing)
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            StatusLine(status: viewModel.status)

            if viewModel.mode == .viewing && !viewModel.isSessionActive {
                DeviceList(devices: viewModel.devices, peers: viewModel.peers) { viewModel.connect(to: $0) }
            }

            if viewModel.isSessionActive {
                Button("connection.disconnect", role: .destructive) { viewModel.disconnect() }
                if viewModel.mode == .viewing {
                    Text(viewModel.hotkeyUnavailable ? "error.hotkey.unavailable" : "connection.hotkeyHint")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(.secondary)
                }
            }

            if !viewModel.peers.isEmpty {
                PeerList(peers: viewModel.peers, revoke: viewModel.revoke, remove: viewModel.remove)
            }

            if let error = viewModel.error {
                Text(error.message)
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.error)
                if error.needsScreenRecordingPermission {
                    Button("permission.screenRecording.open") { viewModel.openScreenRecordingSettings() }
                }
            }
        }
    }
}

private struct StatusLine: View {
    let status: ConnectionViewModel.Status

    var body: some View {
        Group {
            switch status {
            case .idle:
                EmptyView()
            case .waitingForViewer:
                Text("connection.status.waiting")
            case .connecting(let peer):
                Text("connection.status.connecting \(peer)")
            case .connected(let peer):
                Text("connection.status.connected \(peer)")
            }
        }
        .font(Theme.Typography.caption)
        .foregroundStyle(.secondary)
    }
}

private struct DeviceList: View {
    let devices: [DiscoveredDevice]
    let peers: [TrustedPeer]
    let connect: (DiscoveredDevice) -> Void

    var body: some View {
        if devices.isEmpty {
            Text("connection.searching")
                .font(Theme.Typography.caption)
                .foregroundStyle(.secondary)
        }
        ForEach(devices) { device in
            HStack(spacing: Theme.Spacing.small) {
                VStack(alignment: .leading) {
                    Text(verbatim: device.advertisement.name)
                    Text(platformName(device.advertisement.platform))
                        .font(Theme.Typography.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button("connection.connect") { connect(device) }
            }
        }
    }
}

private func platformName(_ platform: PeerPlatform) -> LocalizedStringKey {
    platform == .windows ? "connection.platform.windows" : "connection.platform.mac"
}

private struct PeerList: View {
    let peers: [TrustedPeer]
    let revoke: (TrustedPeer) -> Void
    let remove: (TrustedPeer) -> Void

    var body: some View {
        DisclosureGroup("connection.paired") {
            ForEach(peers) { peer in
                HStack(spacing: Theme.Spacing.small) {
                    Text(verbatim: peer.name)
                    if peer.revoked {
                        Text("connection.revoked")
                            .font(Theme.Typography.caption)
                            .foregroundStyle(Theme.Colors.error)
                    }
                    Spacer()
                    if !peer.revoked {
                        Button("connection.revoke") { revoke(peer) }
                    }
                    Button("connection.remove", role: .destructive) { remove(peer) }
                }
                .font(Theme.Typography.caption)
            }
        }
        .font(Theme.Typography.caption)
    }
}
