import SwiftUI

@main
struct OsplayApp: App {
    private let dependencies: AppDependencies
    #if DEBUG
        @State private var developerDisplays: DeveloperDisplaysViewModel
    #endif

    init() {
        let dependencies = AppDependencies()
        self.dependencies = dependencies
        #if DEBUG
            _developerDisplays = State(initialValue: DeveloperDisplaysViewModel(provider: dependencies.virtualDisplays))
        #endif
    }

    var body: some Scene {
        MenuBarExtra {
            #if DEBUG
                MenuContentView(developerDisplays: developerDisplays)
            #else
                MenuContentView()
            #endif
        } label: {
            Label("app.name", systemImage: Theme.Symbol.menuBar)
        }
        .menuBarExtraStyle(.window)
    }
}
