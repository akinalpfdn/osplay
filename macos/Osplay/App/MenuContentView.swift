import AppKit
import CoreBridge
import SwiftUI

struct MenuContentView: View {
    #if DEBUG
        let developerDisplays: DeveloperDisplaysViewModel
        let developerHostTest: DeveloperHostTestViewModel
        let developerLoopback: DeveloperLoopbackViewModel
    #endif
    private let coreVersion = CoreVersion.linked

    var body: some View {
        VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
            Text("app.name")
                .font(Theme.Typography.title)
            Text("menu.coreVersion \(coreVersion.description)")
                .font(Theme.Typography.caption)
                .foregroundStyle(.secondary)
            #if DEBUG
                Divider()
                DeveloperDisplaysView(viewModel: developerDisplays)
                Divider()
                DeveloperHostTestView(viewModel: developerHostTest)
                Divider()
                DeveloperLoopbackView(viewModel: developerLoopback)
            #endif
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
