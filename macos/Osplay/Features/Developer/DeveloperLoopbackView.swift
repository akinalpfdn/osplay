#if DEBUG
    import SwiftUI

    /// Debug-only controls for the viewer loopback.
    struct DeveloperLoopbackView: View {
        @Bindable var viewModel: DeveloperLoopbackViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                Text("developer.loopback.title")
                    .font(Theme.Typography.sectionTitle)
                Toggle("developer.loopback.fullScreen", isOn: $viewModel.fullScreen)
                    .disabled(viewModel.isRunning)
                Toggle("developer.loopback.freeze", isOn: $viewModel.freezeAfterDelay)
                    .disabled(viewModel.isRunning)

                HStack(spacing: Theme.Spacing.small) {
                    Button("developer.hostTest.start") { viewModel.start() }
                        .disabled(viewModel.isRunning)
                    Button("developer.hostTest.stop", role: .destructive) { viewModel.stop() }
                        .disabled(!viewModel.isRunning)
                    Button("developer.loopback.corrupt") { viewModel.corruptNextFrame() }
                        .disabled(!viewModel.isRunning)
                }

                if viewModel.isRunning {
                    Text("developer.loopback.hotkeyHint")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(.secondary)
                    if let fps = viewModel.framesPerSecond {
                        Text("developer.loopback.presented \(fps.formatted(.number.precision(.fractionLength(1))))")
                            .font(Theme.Typography.caption)
                    }
                    Text(
                        "developer.loopback.errors \(Int(viewModel.decodeErrors)) \(viewModel.lastRecoveryMilliseconds?.formatted(.number.precision(.fractionLength(0))) ?? "–")"
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
