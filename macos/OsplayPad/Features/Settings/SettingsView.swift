import SwiftUI

/// The iPad's name, the statistics overlay and the paired computers.
struct SettingsView: View {
    @Bindable var connection: PadConnectionViewModel
    @Environment(\.dismiss) private var dismiss
    /// Apple TV: the paired computer whose actions are shown; the remote cannot reach buttons inside
    /// a row, so a row opens them.
    @State private var managedPeer: TrustedPeer?

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("pad.settings.name", text: Bindable(connection.settings).deviceName)
                        .textInputAutocapitalization(.words)
                        .autocorrectionDisabled()
                } header: {
                    Text("pad.settings.name").foregroundStyle(Theme.Colors.detail)
                } footer: {
                    Text("pad.settings.nameFooter").foregroundStyle(Theme.Colors.detail)
                }
                #if os(tvOS)
                    // The QR code first: an Apple TV is useless without Osplay on a computer, and the
                    // code should not need scrolling to.
                    downloads
                #endif
                Section {
                    Toggle("pad.settings.stats", isOn: Bindable(connection.settings).showsStatsOverlay)
                }
                Section {
                    Toggle("pad.settings.playsSound", isOn: Bindable(connection.settings).playsSound)
                } footer: {
                    Text("pad.settings.playsSoundFooter").foregroundStyle(Theme.Colors.detail)
                }
                Section {
                    if connection.peers.isEmpty {
                        Text("pad.settings.noPaired")
                            .foregroundStyle(Theme.Colors.secondary)
                    }
                    ForEach(connection.peers) { peer in
                        #if os(tvOS)
                            // No swipe on a remote: selecting the row offers revoke and remove.
                            Button {
                                managedPeer = peer
                            } label: {
                                peerName(peer)
                            }
                        #else
                            peerName(peer)
                                .swipeActions {
                                    Button("pad.settings.remove", role: .destructive) { connection.remove(peer) }
                                    if !peer.revoked {
                                        Button("pad.settings.revoke") { connection.revoke(peer) }
                                    }
                                }
                        #endif
                    }
                } header: {
                    Text("pad.settings.paired").foregroundStyle(Theme.Colors.detail)
                } footer: {
                    Text("pad.settings.pairedFooter").foregroundStyle(Theme.Colors.detail)
                }
                #if !os(tvOS)
                    downloads
                #endif
                Section {
                    LabeledContent("pad.settings.version", value: Self.version)
                }
            }
            .navigationTitle("pad.settings.title")
            #if os(tvOS)
                .confirmationDialog(
                    managedPeer?.name ?? "",
                    isPresented: Binding(get: { managedPeer != nil }, set: { if !$0 { managedPeer = nil } }),
                    titleVisibility: .visible, presenting: managedPeer
                ) { peer in
                    if !peer.revoked {
                        Button("pad.settings.revoke") { connection.revoke(peer) }
                    }
                    Button("pad.settings.remove", role: .destructive) { connection.remove(peer) }
                    Button("pad.action.cancel", role: .cancel) {}
                } message: { peer in
                    Text(peer.revoked ? "pad.settings.manageRevoked" : "pad.settings.manage")
                }
            #endif
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("pad.settings.done") { dismiss() }
                }
            }
        }
    }

    /// Where Osplay for the Mac and Windows comes from.
    private var downloads: some View {
        Section {
            DownloadsRow()
        } header: {
            Text("pad.settings.downloads").foregroundStyle(Theme.Colors.detail)
        } footer: {
            Text("pad.settings.downloadsFooter").foregroundStyle(Theme.Colors.detail)
        }
    }

    private func peerName(_ peer: TrustedPeer) -> some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
            Text(peer.name)
                .font(Theme.Typography.body)
            if peer.revoked {
                Text("pad.settings.revoked")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.warning)
            } else {
                #if os(tvOS)
                    // Both states named, so a revoke shows at once.
                    Text("pad.device.paired")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.detail)
                #endif
            }
        }
    }

    private static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }
}
