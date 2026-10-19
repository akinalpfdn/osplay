import SwiftUI

/// The single source of visual values. Views use these tokens and never literal values.
enum Theme {
    enum Spacing {
        static let small: CGFloat = 4
        static let medium: CGFloat = 8
        static let large: CGFloat = 12
    }

    enum Typography {
        static let title = Font.headline
        static let sectionTitle = Font.subheadline.weight(.semibold)
        static let caption = Font.caption
    }

    enum Colors {
        static let error = Color(.errorText)
    }

    enum Size {
        static let menuWidth: CGFloat = 320
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
        static let copy = "doc.on.doc"
    }
}
