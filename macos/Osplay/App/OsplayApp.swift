import SwiftUI

@main
struct OsplayApp: App {
    private let dependencies: AppDependencies
    #if DEBUG
        @State private var developerDisplays: DeveloperDisplaysViewModel
        @State private var developerHostTest: DeveloperHostTestViewModel
    #endif

    init() {
        let dependencies = AppDependencies()
        self.dependencies = dependencies
        #if DEBUG
            _developerDisplays = State(initialValue: DeveloperDisplaysViewModel(provider: dependencies.virtualDisplays))
            _developerHostTest = State(
                initialValue: DeveloperHostTestViewModel(
                    provider: dependencies.virtualDisplays, captures: dependencies.captures,
                    encoders: dependencies.encoders, permissions: dependencies.permissions))
        #endif
    }

    var body: some Scene {
        MenuBarExtra {
            #if DEBUG
                MenuContentView(developerDisplays: developerDisplays, developerHostTest: developerHostTest)
            #else
                MenuContentView()
            #endif
        } label: {
            Label("app.name", systemImage: Theme.Symbol.menuBar)
        }
        .menuBarExtraStyle(.window)
    }
}
