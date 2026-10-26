import Foundation
import Observation
import UIKit

/// What the user sets on the iPad, kept in UserDefaults.
@MainActor
@Observable
final class PadSettings {
    private let defaults: UserDefaults

    /// The name computers show for this iPad. iPadOS only tells apps the model ("iPad"), so the
    /// user can name it.
    var deviceName: String {
        didSet { defaults.set(deviceName, forKey: Tunables.Pad.deviceNameKey) }
    }

    var showsStatsOverlay: Bool {
        didSet { defaults.set(showsStatsOverlay, forKey: Tunables.Pad.statsOverlayKey) }
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        deviceName = defaults.string(forKey: Tunables.Pad.deviceNameKey) ?? UIDevice.current.name
        showsStatsOverlay = defaults.bool(forKey: Tunables.Pad.statsOverlayKey)
    }

    /// The name as the protocol carries it, never empty.
    var protocolDeviceName: String {
        let name = ProtocolText.name(deviceName.trimmingCharacters(in: .whitespacesAndNewlines))
        return name.isEmpty ? ProtocolText.name(UIDevice.current.model) : name
    }
}
