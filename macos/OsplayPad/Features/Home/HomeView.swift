import CoreBridge
import SwiftUI

/// The computers that share their screens on this network; tapping one shows it on the iPad.
struct HomeView: View {
    let connection: PadConnectionViewModel
    @State private var showsSettings = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: Theme.Spacing.large) {
                    Text("pad.home.subtitle")
                        .font(Theme.Typography.body)
                        .foregroundStyle(Theme.Colors.secondary)
                    if let failure = connection.failure {
                        FailureCard(failure: failure, dismiss: connection.dismissFailure)
                    }
                    if case .reconnecting(let peer) = connection.status {
                        ReconnectingCard(peer: peer, cancel: connection.disconnect)
                    }
                    Text("pad.home.nearby")
                        .font(Theme.Typography.rowTitle)
                    if connection.devices.isEmpty {
                        SearchingCard()
                    } else {
                        ForEach(connection.devices) { device in
                            DeviceRow(
                                name: connection.displayName(of: device), platform: device.advertisement.platform,
                                state: connection.state(of: device)
                            ) { connection.connect(to: device) }
                        }
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(Theme.Spacing.large)
            }
            .background(Theme.Colors.background)
            .navigationTitle("pad.home.title")
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button {
                        showsSettings = true
                    } label: {
                        Label("pad.settings.title", systemImage: Theme.Symbol.settings)
                    }
                }
            }
            .sheet(isPresented: $showsSettings) {
                SettingsView(connection: connection)
            }
        }
        .onAppear(perform: connection.start)
    }
}

/// One computer: its platform, name, and what tapping it does.
private struct DeviceRow: View {
    let name: String
    let platform: PeerPlatform
    let state: PadConnectionViewModel.DeviceState
    let connect: () -> Void

    var body: some View {
        Button(action: connect) {
            HStack(spacing: Theme.Spacing.medium) {
                Image(systemName: Theme.Symbol.device(platform))
                    .font(Theme.Typography.symbol)
                    .foregroundStyle(Theme.Colors.accent)
                    .frame(width: Theme.Size.symbolColumn)
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                    Text(name)
                        .font(Theme.Typography.rowTitle)
                        .foregroundStyle(.primary)
                    Text(detail)
                        .font(Theme.Typography.rowDetail)
                        .foregroundStyle(state == .needsUpdate ? Theme.Colors.warning : Theme.Colors.secondary)
                }
                Spacer(minLength: Theme.Spacing.medium)
            }
            .padding(Theme.Spacing.large)
            .background(Theme.Colors.card, in: RoundedRectangle(cornerRadius: Theme.Radius.card))
        }
        .buttonStyle(.plain)
        .disabled(state == .incompatible)
        .accessibilityHint(Text("pad.device.connect"))
    }

    private var detail: LocalizedStringKey {
        switch state {
        case .paired: "pad.device.paired"
        case .new: "pad.device.new"
        case .needsUpdate: "pad.device.needsUpdate"
        case .incompatible: "pad.device.incompatible"
        }
    }
}

/// Shown while no computer shares its screen.
private struct SearchingCard: View {
    var body: some View {
        HStack(alignment: .top, spacing: Theme.Spacing.medium) {
            Image(systemName: Theme.Symbol.searching)
                .font(Theme.Typography.symbol)
                .foregroundStyle(Theme.Colors.secondary)
                .symbolEffect(.pulse)
                .frame(width: Theme.Size.symbolColumn)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text("pad.home.searching")
                    .font(Theme.Typography.body)
                Text("pad.home.sameNetwork")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(Theme.Spacing.large)
        .background(Theme.Colors.card, in: RoundedRectangle(cornerRadius: Theme.Radius.card))
    }
}

/// The connection dropped; Osplay connects again when the computer is back.
private struct ReconnectingCard: View {
    let peer: String
    let cancel: () -> Void

    var body: some View {
        HStack(spacing: Theme.Spacing.medium) {
            ProgressView()
                .frame(width: Theme.Size.symbolColumn)
            Text(String(localized: "pad.status.reconnecting \(peer)"))
                .font(Theme.Typography.body)
            Spacer(minLength: Theme.Spacing.medium)
            Button("pad.action.cancel", action: cancel)
        }
        .padding(Theme.Spacing.large)
        .background(Theme.Colors.card, in: RoundedRectangle(cornerRadius: Theme.Radius.card))
    }
}

/// What went wrong and, for the local network, where to allow it.
private struct FailureCard: View {
    let failure: PadFailure
    let dismiss: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Text(failure.message)
                .font(Theme.Typography.body)
                .foregroundStyle(Theme.Colors.error)
                .fixedSize(horizontal: false, vertical: true)
            HStack(spacing: Theme.Spacing.medium) {
                if failure.needsLocalNetworkPermission, let settings = URL(string: UIApplication.openSettingsURLString)
                {
                    Link("pad.error.openSettings", destination: settings)
                }
                Spacer()
                Button("pad.error.dismiss", action: dismiss)
            }
        }
        .padding(Theme.Spacing.large)
        .background(Theme.Colors.card, in: RoundedRectangle(cornerRadius: Theme.Radius.card))
    }
}
