/// Opens the menu by itself once, after the first launch, so a new user sees where Osplay lives
/// (user decision 2026-11-23: people did not notice it had started).
@MainActor
final class FirstLaunchMenu {
    private let settings: AppSettings
    private let openMenu: () -> Void

    init(settings: AppSettings, openMenu: @escaping () -> Void) {
        self.settings = settings
        self.openMenu = openMenu
    }

    /// At launch. While the permissions window is open the menu waits for it to close: the window
    /// would take focus and close the menu at once.
    func launched(showsOnboarding: Bool) {
        if !showsOnboarding { open() }
    }

    func onboardingClosed() {
        open()
    }

    private func open() {
        guard !settings.hasShownMenu else { return }
        settings.hasShownMenu = true
        openMenu()
    }
}
