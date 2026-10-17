import AppKit
import SwiftUI

struct MenuContentView: View {
    private let coreVersion = CoreVersion.linked

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Text("app.name")
                .font(Theme.Typography.title)
            Text("menu.coreVersion \(coreVersion.description)")
                .font(Theme.Typography.caption)
                .foregroundStyle(.secondary)
            Divider()
            Button("menu.quit") {
                NSApplication.shared.terminate(nil)
            }
            .keyboardShortcut("q")
        }
        .padding(Theme.Spacing.large)
        .frame(width: Theme.Size.menuWidth, alignment: .leading)
    }
}
