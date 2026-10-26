import CoreBridge
import SwiftUI

/// The iPad app's single source of visual values. Views use these tokens and never literal values.
enum Theme {
    enum Spacing {
        static let small: CGFloat = 6
        static let medium: CGFloat = 12
        static let large: CGFloat = 20
        static let extraLarge: CGFloat = 32
    }

    enum Typography {
        static let rowTitle = Font.title3.weight(.semibold)
        static let rowDetail = Font.subheadline
        static let body = Font.body
        static let caption = Font.footnote
        static let symbol = Font.title
        static let statusSymbol = Font.largeTitle
        /// The six pairing digits: large and evenly spaced so both screens are easy to compare.
        static let pairingCode = Font.system(size: 56, weight: .semibold, design: .monospaced)
    }

    enum Colors {
        static let accent = Color.accentColor
        static let secondary = Color.secondary
        static let error = Color.red
        static let warning = Color.orange
        static let card = Color(.secondarySystemGroupedBackground)
        static let background = Color(.systemGroupedBackground)
    }

    enum Radius {
        static let card: CGFloat = 16
    }

    enum Size {
        static let symbolColumn: CGFloat = 44
    }

    enum Symbol {
        static let mac = "laptopcomputer"
        static let windows = "pc"
        static let tablet = "ipad.landscape"
        static let settings = "gearshape"
        static let disconnect = "xmark"
        static let statistics = "chart.bar.xaxis"
        static let searching = "dot.radiowaves.left.and.right"
        static let update = "arrow.triangle.2.circlepath"

        /// The symbol for a computer's platform.
        static func device(_ platform: PeerPlatform) -> String {
            switch platform {
            case .macOS: mac
            case .windows: windows
            case .iPadOS, .android: tablet
            }
        }
    }

    /// The full-screen viewer.
    enum Viewer {
        static let background = Color.black
        static let controlBackground = Material.regular
    }

    /// The stream statistics in the viewer's top-left corner.
    enum StatsOverlay {
        static let fontSize: CGFloat = 15
        static let padding: CGFloat = 8
        static let margin: CGFloat = 16
        static let cornerRadius: CGFloat = 8
        static let text = UIColor.white
        static let background = UIColor.black.withAlphaComponent(0.6)
    }
}
