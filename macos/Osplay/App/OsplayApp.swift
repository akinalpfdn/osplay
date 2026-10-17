import SwiftUI

@main
struct OsplayApp: App {
    var body: some Scene {
        MenuBarExtra {
            MenuContentView()
        } label: {
            Label("app.name", systemImage: Theme.Symbol.menuBar)
        }
        .menuBarExtraStyle(.window)
    }
}
