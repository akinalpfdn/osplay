import Foundation

/// The iPad app's tunable values on top of the shared ones.
extension Tunables {
    enum Pad {
        /// What this iPad announces it can receive.
        static let maximumWidth = 7680
        static let maximumHeight = 4320
        static let maximumRefreshHz = 120
        /// One screen, so one stream.
        static let maximumStreams = 1
        /// Physical size is only a hint for the host's scaling; iPads have about this density.
        static let assumedPixelsPerInch = 264.0
        /// Pause before trying a computer again after the connection dropped.
        static let reconnectDelay: Duration = .seconds(2)
        /// The viewer's controls hide again after this long.
        static let controlsHideDelay: Duration = .seconds(4)
        /// UserDefaults keys.
        static let deviceNameKey = "deviceName"
        static let statsOverlayKey = "showsStatsOverlay"
    }
}
