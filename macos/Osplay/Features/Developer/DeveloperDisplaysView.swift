#if DEBUG
    import SwiftUI

    /// Debug-only controls for creating test virtual displays.
    struct DeveloperDisplaysView: View {
        @Bindable var viewModel: DeveloperDisplaysViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                Text("developer.title")
                    .font(Theme.Typography.sectionTitle)

                Picker("developer.resolution", selection: $viewModel.selectedResolution) {
                    ForEach(viewModel.resolutions, id: \.self) { size in
                        Text(verbatim: size.description).tag(size)
                    }
                }
                Toggle("developer.hiDPI", isOn: $viewModel.isHiDPI)
                Toggle("developer.crispDefault", isOn: $viewModel.startsAtCrispResolution)
                    .disabled(!viewModel.isHiDPI)

                HStack(spacing: Theme.Spacing.small) {
                    Button("developer.createOne") { viewModel.createDisplays(count: 1) }
                    Button("developer.createTwo") { viewModel.createDisplays(count: 2) }
                    Button("developer.removeAll", role: .destructive) { viewModel.removeAll() }
                        .disabled(viewModel.rows.isEmpty)
                }

                if viewModel.rows.isEmpty {
                    Text("developer.noDisplays")
                        .font(Theme.Typography.caption)
                        .foregroundStyle(.secondary)
                }
                ForEach(viewModel.rows) { row in
                    Picker(
                        row.name,
                        selection: Binding(
                            get: { row.current },
                            set: { resolution in
                                if let resolution { viewModel.select(resolution, forDisplay: row.id) }
                            }
                        )
                    ) {
                        ForEach(row.available, id: \.self) { resolution in
                            ResolutionLabel(resolution: resolution).tag(Optional(resolution))
                        }
                    }
                }

                if let errorMessage = viewModel.errorMessage {
                    Text(errorMessage)
                        .font(Theme.Typography.caption)
                        .foregroundStyle(Theme.Colors.error)
                }
            }
            .onAppear { viewModel.refresh() }
        }
    }

    private struct ResolutionLabel: View {
        let resolution: DisplayResolution

        var body: some View {
            if resolution.isHiDPI {
                Text("resolution.hiDPI \(resolution.points.description) \(resolution.pixels.description)")
            } else {
                Text(verbatim: resolution.points.description)
            }
        }
    }
#endif
