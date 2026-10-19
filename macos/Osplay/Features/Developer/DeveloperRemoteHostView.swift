#if DEBUG
    import SwiftUI

    /// Debug-only controls for hosting a viewer on another machine.
    struct DeveloperRemoteHostView: View {
        @Bindable var viewModel: DeveloperRemoteHostViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                Text("developer.remoteHost.title")
                    .font(Theme.Typography.sectionTitle)

                HStack(spacing: Theme.Spacing.small) {
                    Button("developer.hostTest.start") { viewModel.start() }
                        .disabled(viewModel.isRunning)
                    Button("developer.hostTest.stop", role: .destructive) { viewModel.stop() }
                        .disabled(!viewModel.isRunning)
                }

                if viewModel.isRunning {
                    Text("developer.remoteHost.hint")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(.secondary)
                    ForEach(viewModel.commands, id: \.self) { command in
                        HStack(spacing: Theme.Spacing.small) {
                            Text(verbatim: command)
                                .font(Theme.Typography.caption.monospaced())
                            Button {
                                viewModel.copy(command)
                            } label: {
                                Label("developer.remoteHost.copy", systemImage: Theme.Symbol.copy)
                            }
                        }
                    }
                    Text(
                        "developer.remoteHost.status \(viewModel.streamCount) \((Double(viewModel.bitrateKbps) / 1000).formatted(.number.precision(.fractionLength(1))))"
                    )
                    .font(Theme.Typography.caption)
                    .foregroundStyle(.secondary)
                }

                if let errorMessage = viewModel.errorMessage {
                    Text(errorMessage)
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.error)
                    if viewModel.needsScreenRecordingPermission {
                        Button("permission.screenRecording.open") { viewModel.openScreenRecordingSettings() }
                    }
                }
            }
        }
    }
#endif
