import SwiftUI

@main
struct OsplayApp: App {
    private let dependencies: AppDependencies
    #if DEBUG
        @State private var developerDisplays: DeveloperDisplaysViewModel
        @State private var developerHostTest: DeveloperHostTestViewModel
        @State private var developerLoopback: DeveloperLoopbackViewModel
        @State private var developerRemoteHost: DeveloperRemoteHostViewModel
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
            _developerLoopback = State(initialValue: DeveloperLoopbackViewModel(dependencies: dependencies))
            _developerRemoteHost = State(initialValue: DeveloperRemoteHostViewModel(dependencies: dependencies))
        #endif
    }

    var body: some Scene {
        MenuBarExtra {
            #if DEBUG
                MenuContentView(
                    developerDisplays: developerDisplays, developerHostTest: developerHostTest,
                    developerLoopback: developerLoopback, developerRemoteHost: developerRemoteHost)
            #else
                MenuContentView()
            #endif
        } label: {
            Label("app.name", systemImage: Theme.Symbol.menuBar)
        }
        .menuBarExtraStyle(.window)
    }
}
