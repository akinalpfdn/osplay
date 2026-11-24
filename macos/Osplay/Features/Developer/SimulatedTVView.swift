#if DEBUG
    import SwiftUI

    /// Debug-only choice of the TV this Mac poses as when it is used as a display.
    struct SimulatedTVView: View {
        @Bindable var connection: ConnectionViewModel

        var body: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                Text("developer.tv.title")
                    .font(Theme.Typography.sectionTitle)
                Picker("developer.tv.picker", selection: $connection.simulatedTV) {
                    ForEach(SimulatedTV.allCases) { tv in
                        Text(tv.label).tag(tv)
                    }
                }
                Text("developer.tv.hint")
                    .font(Theme.Typography.caption)
                    .foregroundStyle(Theme.Colors.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }
#endif
