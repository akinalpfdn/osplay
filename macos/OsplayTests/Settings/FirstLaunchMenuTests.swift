import Foundation
import Testing

@testable import Osplay

@MainActor
@Suite("First launch menu")
struct FirstLaunchMenuTests {
    private final class Opened {
        var count = 0
    }

    private func freshSettings() throws -> AppSettings {
        let name = "osplay-tests-\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: name))
        defaults.removePersistentDomain(forName: name)
        return AppSettings(defaults: defaults)
    }

    @Test("Opens the menu at the first launch when no permissions window shows")
    func opensAtFirstLaunch() throws {
        let settings = try freshSettings()
        let opened = Opened()
        FirstLaunchMenu(settings: settings) { opened.count += 1 }.launched(showsOnboarding: false)
        #expect(opened.count == 1)
        #expect(settings.hasShownMenu)
    }

    @Test("Waits for the permissions window to close")
    func waitsForOnboarding() throws {
        let opened = Opened()
        let menu = FirstLaunchMenu(settings: try freshSettings()) { opened.count += 1 }
        menu.launched(showsOnboarding: true)
        #expect(opened.count == 0)
        menu.onboardingClosed()
        #expect(opened.count == 1)
    }

    @Test("Opens only once, also when the permissions window is opened again later")
    func opensOnce() throws {
        let settings = try freshSettings()
        let opened = Opened()
        let menu = FirstLaunchMenu(settings: settings) { opened.count += 1 }
        menu.launched(showsOnboarding: false)
        menu.onboardingClosed()
        FirstLaunchMenu(settings: settings) { opened.count += 1 }.launched(showsOnboarding: false)
        #expect(opened.count == 1)
    }
}
