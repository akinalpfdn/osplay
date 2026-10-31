import SwiftUI

/// The iPad's name, the statistics overlay and the paired computers.
struct SettingsView: View {
    @Bindable var connection: PadConnectionViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("pad.settings.name", text: Bindable(connection.settings).deviceName)
                        .textInputAutocapitalization(.words)
                        .autocorrectionDisabled()
                } header: {
                    Text("pad.settings.name")
                } footer: {
                    Text("pad.settings.nameFooter")
                }
                Section {
                    Toggle("pad.settings.stats", isOn: Bindable(connection.settings).showsStatsOverlay)
                }
                Section {
                    if connection.peers.isEmpty {
                        Text("pad.settings.noPaired")
                            .foregroundStyle(Theme.Colors.secondary)
                    }
                    ForEach(connection.peers) { peer in
                        VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                            Text(peer.name)
                                .font(Theme.Typography.body)
                            if peer.revoked {
                                Text("pad.settings.revoked")
                                    .font(Theme.Typography.caption)
                                    .foregroundStyle(Theme.Colors.warning)
                            }
                        }
                        .swipeActions {
                            Button("pad.settings.remove", role: .destructive) { connection.remove(peer) }
                            if !peer.revoked {
                                Button("pad.settings.revoke") { connection.revoke(peer) }
                            }
                        }
                    }
                } header: {
                    Text("pad.settings.paired")
                } footer: {
                    Text("pad.settings.pairedFooter")
                }
                Section {
                    LabeledContent("pad.settings.version", value: Self.version)
                }
            }
            .navigationTitle("pad.settings.title")
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("pad.settings.done") { dismiss() }
                }
            }
        }
    }

    private static var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
    }
}
