import Foundation
import Observation

/// User settings (DEVPLAN: UserDefaults on macOS). Every change is saved at once.
@MainActor
@Observable
final class AppSettings {
    var qualityPreset: QualityPreset {
        didSet { defaults.set(qualityPreset.rawValue, forKey: Key.qualityPreset) }
    }

    /// Hides and shows the viewer.
    var viewerHotkey: Hotkey {
        didSet { save(viewerHotkey, forKey: Key.viewerHotkey) }
    }

    /// A small box with fps, bitrate, round trip, loss and latency over the watched display.
    var showsStatsOverlay: Bool {
        didSet { defaults.set(showsStatsOverlay, forKey: Key.statsOverlay) }
    }

    /// The permissions window was finished once; afterwards it opens only when one is missing.
    var hasCompletedOnboarding: Bool {
        didSet { defaults.set(hasCompletedOnboarding, forKey: Key.onboarding) }
    }

    /// Per paired device (by device ID), how its monitors' virtual displays start.
    private(set) var hiDPIChoices: [String: HiDPIChoice] {
        didSet { save(hiDPIChoices, forKey: Key.hiDPIChoices) }
    }

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        qualityPreset = defaults.string(forKey: Key.qualityPreset).flatMap(QualityPreset.init) ?? .high
        viewerHotkey = Self.load(Hotkey.self, from: defaults, forKey: Key.viewerHotkey) ?? Tunables.Viewer.toggleHotkey
        showsStatsOverlay = defaults.bool(forKey: Key.statsOverlay)
        hasCompletedOnboarding = defaults.bool(forKey: Key.onboarding)
        hiDPIChoices = Self.load([String: HiDPIChoice].self, from: defaults, forKey: Key.hiDPIChoices) ?? [:]
        placements = Self.load([String: DisplayPlacement].self, from: defaults, forKey: Key.placements) ?? [:]
    }

    /// Per paired device (by device ID), where its displays sit next to this Mac.
    private(set) var placements: [String: DisplayPlacement] {
        didSet { save(placements, forKey: Key.placements) }
    }

    func placement(for deviceID: String) -> DisplayPlacement {
        placements[deviceID] ?? .right
    }

    func setPlacement(_ placement: DisplayPlacement, for deviceID: String) {
        placements[deviceID] = placement == .right ? nil : placement
    }

    func hiDPIChoice(for deviceID: String) -> HiDPIChoice {
        hiDPIChoices[deviceID] ?? .automatic
    }

    func setHiDPIChoice(_ choice: HiDPIChoice, for deviceID: String) {
        hiDPIChoices[deviceID] = choice == .automatic ? nil : choice
    }

    private func save(_ value: some Encodable, forKey key: String) {
        // Encoding plain value types cannot fail.
        if let data = try? JSONEncoder().encode(value) {
            defaults.set(data, forKey: key)
        }
    }

    private static func load<Value: Decodable>(
        _ type: Value.Type, from defaults: UserDefaults, forKey key: String
    )
        -> Value?
    {
        // A value that no longer decodes (an older format) falls back to the default.
        defaults.data(forKey: key).flatMap { try? JSONDecoder().decode(type, from: $0) }
    }

    private enum Key {
        static let qualityPreset = "qualityPreset"
        static let viewerHotkey = "viewerHotkey"
        static let statsOverlay = "showsStatsOverlay"
        static let hiDPIChoices = "hiDPIChoices"
        static let onboarding = "hasCompletedOnboarding"
        static let placements = "displayPlacements"
    }
}
