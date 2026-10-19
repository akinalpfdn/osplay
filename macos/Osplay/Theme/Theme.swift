import SwiftUI

/// The single source of visual values. Views use these tokens and never literal values.
enum Theme {
    enum Spacing {
        static let small: CGFloat = 4
        static let medium: CGFloat = 8
        static let large: CGFloat = 12
        static let extraLarge: CGFloat = 20
    }

    enum Typography {
        static let title = Font.headline
        static let sectionTitle = Font.subheadline.weight(.semibold)
        static let caption = Font.caption
        static let rowTitle = Font.body
        static let windowTitle = Font.title2.weight(.semibold)
        static let symbol = Font.title3
        static let body = Font.body
        /// The six pairing digits: large and evenly spaced so both screens are easy to compare.
        static let pairingCode = Font.system(size: 34, weight: .semibold, design: .monospaced)
    }

    enum Colors {
        static let error = Color(.errorText)
        static let accent = Color.accentColor
        static let success = Color.green
        static let warning = Color.orange
        static let secondary = Color.secondary
        /// Grouped cards in the menu window, like Control Center modules.
        static let card = Color.primary.opacity(0.06)
    }

    enum Radius {
        static let card: CGFloat = 10
    }

    enum Size {
        static let menuWidth: CGFloat = 320
        static let pairingPanelWidth: CGFloat = 300
        static let settingsWidth: CGFloat = 480
        static let settingsHeight: CGFloat = 320
        static let onboardingWidth: CGFloat = 420
        static let statusDot: CGFloat = 8
        static let symbolColumn: CGFloat = 24
        static let hotkeyField: CGFloat = 120
    }

    /// The stats box over a watched display: readable on any picture.
    enum StatsOverlay {
        static let background = Color.black.opacity(0.6)
        static let text = Color.white
        static let fontSize: CGFloat = 12
        static let padding: CGFloat = 6
        static let margin: CGFloat = 12
        static let cornerRadius: CGFloat = 6
    }

    enum Viewer {
        /// Letterbox bars and the window behind the first frame.
        static let background = Color.black
    }

    /// Debug test pattern shown on shared displays: full-frame motion and high-contrast edges.
    enum TestPattern {
        static let background = Color.black
        static let bar = Color.white
        static let accent = Color.orange
        static let barWidthFraction: CGFloat = 0.08
        /// Seconds for the bar to cross the display once.
        static let sweepDuration: Double = 2
    }

    enum Symbol {
        static let menuBar = "display.2"
        static let mac = "laptopcomputer"
        static let windows = "pc"
        static let sharing = "display.2"
        static let viewing = "rectangle.on.rectangle"
        static let paired = "checkmark.seal.fill"
        static let warning = "exclamationmark.triangle.fill"
        static let granted = "checkmark.circle.fill"
        static let settings = "gearshape"
        static let quit = "power"
        static let general = "gearshape"
        static let displays = "display"
        static let devices = "laptopcomputer.and.iphone"
        static let advanced = "wrench.and.screwdriver"
        static let screenRecording = "rectangle.dashed.badge.record"
        static let localNetwork = "network"
    }
}
