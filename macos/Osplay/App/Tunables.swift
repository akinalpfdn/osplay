import Foundation

/// Every tunable value of the macOS app. Protocol constants live in `core`, not here.
enum Tunables {
    enum VirtualDisplay {
        /// Mac host: up to two displays (DEVPLAN, MVP scope).
        static let maximumDisplayCount = 2
        static let pixelWidthRange = 640...7680
        static let pixelHeightRange = 480...4320
        static let refreshRateRange = 24.0...240.0
        /// "Looks like" sizes offered as fractions of the remote monitor's native pixel size.
        /// macOS adds its own standard sizes on top of these.
        static let looksLikeScaleFactors: [Double] = [1.0, 0.75, 0.5]
        /// How long to wait for a new display's modes to become visible to the app.
        static let readinessTimeout: Duration = .seconds(3)
        static let readinessPollInterval: Duration = .milliseconds(50)
    }

    #if DEBUG
        enum Developer {
            /// Peer ID for test displays, so their arrangement persists between debug sessions.
            static let testPeerID = Data("osplay-developer-test-peer".utf8)
            static let testRefreshRate = 60.0
            /// 27-inch 16:9 panel; only influences which sizes macOS recommends.
            static let testPhysicalSizeMillimeters = CGSize(width: 597, height: 336)
            static let testResolutions: [DisplaySize] = [
                DisplaySize(width: 2560, height: 1440),
                DisplaySize(width: 1920, height: 1080),
                DisplaySize(width: 2560, height: 1664),
                DisplaySize(width: 3840, height: 2160),
            ]
        }
    #endif
}
