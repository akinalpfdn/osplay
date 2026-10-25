import AppKit
import Carbon.HIToolbox
import Foundation
import Testing

@testable import Osplay

@MainActor
@Suite("Settings")
struct AppSettingsTests {
    private func freshDefaults() throws -> UserDefaults {
        let name = "osplay-tests-\(UUID().uuidString)"
        let defaults = try #require(UserDefaults(suiteName: name))
        defaults.removePersistentDomain(forName: name)
        return defaults
    }

    @Test("Defaults: High quality, ⌃⌥⌘V, no overlay, automatic HiDPI")
    func defaults() throws {
        let settings = AppSettings(defaults: try freshDefaults())
        #expect(settings.qualityPreset == .high)
        #expect(settings.streamSize == .hd1080)
        #expect(settings.viewerHotkey == Tunables.Viewer.toggleHotkey)
        #expect(!settings.showsStatsOverlay)
        #expect(settings.hiDPIChoice(for: "abc") == .automatic)
    }

    @Test("Every setting survives a restart")
    func persistence() throws {
        let defaults = try freshDefaults()
        let settings = AppSettings(defaults: defaults)
        settings.qualityPreset = .ultra
        settings.viewerHotkey = Hotkey(keyCode: UInt32(kVK_ANSI_K), modifiers: [.command, .shift])
        settings.showsStatsOverlay = true
        settings.setHiDPIChoice(.never, for: "abc")
        settings.hasCompletedOnboarding = true
        settings.excludedDisplays = ["Built-in Retina Display|0|0"]
        settings.streamSize = .qhd1440

        let reloaded = AppSettings(defaults: defaults)
        #expect(reloaded.excludedDisplays == ["Built-in Retina Display|0|0"])
        #expect(reloaded.streamSize == .qhd1440)
        #expect(reloaded.qualityPreset == .ultra)
        #expect(reloaded.viewerHotkey == settings.viewerHotkey)
        #expect(reloaded.showsStatsOverlay)
        #expect(reloaded.hiDPIChoice(for: "abc") == .never)
        #expect(reloaded.hasCompletedOnboarding)
    }

    @Test("Quality presets scale the bitrate and stay within the encoder's range")
    func presets() {
        let balanced = QualityPreset.balanced.bitrateKbps(width: 2560, height: 1440, frameRate: 60)
        let high = QualityPreset.high.bitrateKbps(width: 2560, height: 1440, frameRate: 60)
        let ultra = QualityPreset.ultra.bitrateKbps(width: 2560, height: 1440, frameRate: 60)
        #expect(balanced < high && high < ultra)
        #expect(QualityPreset.ultra.bitrateKbps(width: 7680, height: 4320, frameRate: 240) == 80_000)
    }

    @Test("A shortcut needs ⌃, ⌥ or ⌘; modifier keys alone are not shortcuts")
    func hotkeyFromKeyPress() {
        #expect(Hotkey(keyCode: UInt16(kVK_ANSI_V), flags: [.control, .option, .command]) != nil)
        #expect(Hotkey(keyCode: UInt16(kVK_ANSI_V), flags: [.shift]) == nil)
        #expect(Hotkey(keyCode: UInt16(kVK_ANSI_V), flags: []) == nil)
        #expect(Hotkey(keyCode: UInt16(kVK_Command), flags: [.command]) == nil)
    }

    @Test("Shortcuts read like macOS menus")
    func hotkeyText() {
        let hotkey = Hotkey(keyCode: UInt32(kVK_F5), modifiers: [.command, .control, .option, .shift])
        #expect(hotkey.displayText == "⌃⌥⇧⌘F5")
    }

    @Test("Stream sizes fit the monitor into their box; Native keeps it")
    func streamSizes() {
        #expect(StreamSizePreset.hd1080.streamSize(width: 3420, height: 2214) == (1668, 1080))
        #expect(StreamSizePreset.hd720.streamSize(width: 3840, height: 2160) == (1280, 720))
        #expect(StreamSizePreset.qhd1440.streamSize(width: 1920, height: 1080) == (1920, 1080))
        #expect(StreamSizePreset.native.streamSize(width: 3420, height: 2214) == (3420, 2214))
    }
}
