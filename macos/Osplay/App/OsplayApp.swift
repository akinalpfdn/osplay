import SwiftUI

@main
struct OsplayApp: App {
    @NSApplicationDelegateAdaptor private var delegate: AppDelegate
    private let dependencies: AppDependencies
    @State private var connection: ConnectionViewModel
    @State private var settings: SettingsViewModel
    private let onboarding: OnboardingWindow
    #if DEBUG
        @State private var developerDisplays: DeveloperDisplaysViewModel
        @State private var developerHostTest: DeveloperHostTestViewModel
        @State private var developerLoopback: DeveloperLoopbackViewModel
    #endif

    init() {
        let dependencies = AppDependencies()
        self.dependencies = dependencies
        let connection = ConnectionViewModel(dependencies: dependencies)
        _connection = State(initialValue: connection)
        let onboardingModel = OnboardingViewModel(
            permissions: dependencies.permissions, localNetworkCheck: BonjourLocalNetworkCheck(),
            settings: dependencies.settings)
        let onboarding = OnboardingWindow(viewModel: onboardingModel)
        self.onboarding = onboarding
        _settings = State(
            initialValue: SettingsViewModel(
                settings: dependencies.settings, connection: connection, launchAtLogin: SystemLaunchAtLogin(),
                hotkeys: dependencies.hotkeys, logs: UnifiedLogExporter(), showPermissions: onboarding.show))
        AppDelegate.onLaunch = {
            if onboardingModel.needsAttention { onboarding.show() }
        }
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
                MenuView(
                    connection: connection, settings: dependencies.settings, openOnboarding: onboarding.show,
                    developer: DeveloperTools(view: AnyView(developerSections)))
            #else
                MenuView(connection: connection, settings: dependencies.settings, openOnboarding: onboarding.show)
            #endif
        } label: {
            Label("app.name", systemImage: Theme.Symbol.menuBar)
        }
        .menuBarExtraStyle(.window)

        Settings {
            SettingsView(viewModel: settings)
        }
    }

    #if DEBUG
        private var developerSections: some View {
            VStack(alignment: .leading, spacing: Theme.Spacing.medium) {
                DeveloperDisplaysView(viewModel: developerDisplays)
                Divider()
                DeveloperHostTestView(viewModel: developerHostTest)
                Divider()
                DeveloperLoopbackView(viewModel: developerLoopback)
            }
        }
    #endif
}

/// Opens the permissions window at launch when something is missing.
final class AppDelegate: NSObject, NSApplicationDelegate {
    @MainActor static var onLaunch: (() -> Void)?

    @MainActor
    func applicationDidFinishLaunching(_ notification: Notification) {
        Self.onLaunch?()
    }
}
