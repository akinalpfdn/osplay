#if DEBUG
    import SwiftUI

    /// Debug-only controls for the host media test.
    struct DeveloperHostTestView: View {
        @Bindable var viewModel: DeveloperHostTestViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                Text("developer.hostTest.title")
                    .font(Theme.Typography.sectionTitle)

                Picker("developer.hostTest.streams", selection: $viewModel.streamCount) {
                    Text(verbatim: "1").tag(1)
                    Text(verbatim: "2").tag(2)
                }
                .pickerStyle(.segmented)
                .disabled(viewModel.isRunning)
                Toggle("developer.hostTest.dump", isOn: $viewModel.dumpsEnabled)
                    .disabled(viewModel.isRunning)

                HStack(spacing: Theme.Spacing.small) {
                    Button("developer.hostTest.start") { viewModel.start() }
                        .disabled(viewModel.isRunning)
                    Button("developer.hostTest.stop", role: .destructive) { viewModel.stop() }
                        .disabled(!viewModel.isRunning)
                    if !viewModel.dumpFiles.isEmpty {
                        Button("developer.hostTest.revealDumps") { viewModel.revealDumps() }
                    }
                }

                ForEach(viewModel.rows) { row in
                    StreamRowView(row: row)
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

    private struct StreamRowView: View {
        let row: DeveloperHostTestViewModel.StreamRow

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.small) {
                Text("developer.hostTest.stream \(Int(row.id)) \(row.size)")
                    .font(Theme.Typography.caption)
                if let pacing = row.pacing {
                    Text(
                        "developer.hostTest.pacing \(pacing.framesPerSecond.formatted(.number.precision(.fractionLength(1)))) \(pacing.p99IntervalMilliseconds.formatted(.number.precision(.fractionLength(1)))) \(pacing.maximumIntervalMilliseconds.formatted(.number.precision(.fractionLength(1))))"
                    )
                    .font(Theme.Typography.caption)
                    .foregroundStyle(.secondary)
                }
                Text(
                    "developer.hostTest.bitrate \((Double(row.bitrateKbps) / 1000).formatted(.number.precision(.fractionLength(1)))) \(row.latencyMilliseconds?.formatted(.number.precision(.fractionLength(1))) ?? "–")"
                )
                .font(Theme.Typography.caption)
                .foregroundStyle(.secondary)
            }
        }
    }
#endif
