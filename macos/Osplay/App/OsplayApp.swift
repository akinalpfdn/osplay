import SwiftUI

@main
struct OsplayApp: App {
    private let dependencies: AppDependencies
    @State private var connection: ConnectionViewModel
    #if DEBUG
        @State private var developerDisplays: DeveloperDisplaysViewModel
        @State private var developerHostTest: DeveloperHostTestViewModel
        @State private var developerLoopback: DeveloperLoopbackViewModel
    #endif

    init() {
        let dependencies = AppDependencies()
        self.dependencies = dependencies
        _connection = State(initialValue: ConnectionViewModel(dependencies: dependencies))
        #if DEBUG
            _developerDisplays = State(initialValue: DeveloperDisplaysViewModel(provider: dependencies.virtualDisplays))
            _developerHostTest = State(
                initialValue: DeveloperHostTestViewModel(
                    provider: dependencies.virtualDisplays, captures: dependencies.captures,
                    encoders: dependencies.encoders, permissions: dependencies.permissions))
            _developerLoopback = State(initialValue: DeveloperLoopbackViewModel(dependencies: dependencies))
        #endif
    }

    var body: some Scene {
        MenuBarExtra {
            #if DEBUG
                MenuContentView(
                    connection: connection, developerDisplays: developerDisplays, developerHostTest: developerHostTest,
                    developerLoopback: developerLoopback)
            #else
                MenuContentView(connection: connection)
            #endif
        } label: {
            Label("app.name", systemImage: Theme.Symbol.menuBar)
        }
        .menuBarExtraStyle(.window)
    }
}
