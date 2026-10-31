import Foundation

/// Tunable values shared by the macOS and iPad apps; each app adds its own in an extension.
/// Protocol constants live in `core`, not here.
enum Tunables {
    enum Viewer {
        /// Refresh rate offered for a display that reports none (built-in panels report 0 Hz).
        static let defaultRefreshRate = 60.0
        /// Displays a viewer offers at most (the protocol's OSPLAY_MAX_MONITORS).
        static let maximumDisplays = 4
        /// Decoder errors closer together than this send one keyframe request (the core limits
        /// requests to one per 250 ms as well).
        static let keyframeRequestSpacing: Duration = .milliseconds(100)
        static let powerAssertionReason = "Osplay is showing another computer's display"
    }

    enum KeyStore {
        static let keychainService = "com.akinalpfdn.osplay"
        static let identityAccount = "identity"
        static let peersAccount = "paired-devices"
        /// Shown in Keychain Access.
        static let itemLabel = "Osplay"
    }
}
