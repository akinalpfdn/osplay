import Carbon.HIToolbox
import CoreBridge
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

    enum Encoding {
        /// Bits per pixel per frame per quality preset: High (0.1) gives ~22 Mbit/s for 1440p60.
        static let balancedBitsPerPixel = 0.06
        static let highBitsPerPixel = 0.1
        static let ultraBitsPerPixel = 0.16
        static let bitrateRangeKbps = 2_000...80_000
        /// Rate control may exceed the average by this factor within one second.
        static let peakBitrateFactor = 1.5
        /// Safety-net keyframe interval in seconds; keyframes normally come on request.
        static let maximumKeyframeInterval = 60.0
        static let profile = H264Profile.high
        /// The boxes a stream is fitted into for each stream size setting (DEVPLAN Phase 13c).
        static let streamBox720 = (width: 1280, height: 720)
        static let streamBox1080 = (width: 1920, height: 1080)
        static let streamBox1440 = (width: 2560, height: 1440)

    }

    enum Viewer {
        /// Refresh rate offered for a display that reports none (built-in panels report 0 Hz).
        static let defaultRefreshRate = 60.0
        /// Displays a viewer offers at most (the protocol's OSPLAY_MAX_MONITORS).
        static let maximumDisplays = 4
        /// The local pointer hides after this many seconds without movement over a viewer.
        static let cursorIdleDelay: TimeInterval = 2
        /// Decoder errors closer together than this send one keyframe request (the core limits
        /// requests to one per 250 ms as well).
        static let keyframeRequestSpacing: Duration = .milliseconds(100)
        static let powerAssertionReason = "Osplay is showing another computer's display"
        /// The default toggle hotkey (User decision: ⌃⌥⌘V); configurable from Phase 11.
        static let toggleHotkey = Hotkey(keyCode: UInt32(kVK_ANSI_V), modifiers: [.control, .option, .command])
        /// The default role swap hotkey (Phase 12b): ⌃⌥⌘S, next to the toggle.
        static let swapHotkey = Hotkey(keyCode: UInt32(kVK_ANSI_S), modifiers: [.control, .option, .command])
        /// The default stats overlay hotkey (Phase 13): ⌃⌥⌘I.
        static let statsHotkey = Hotkey(keyCode: UInt32(kVK_ANSI_I), modifiers: [.control, .option, .command])

        /// Debug loopback windows open at half the stream's size.
        static func loopbackWindowSize(streamWidth: Int, streamHeight: Int) -> CGSize {
            CGSize(width: streamWidth / 2, height: streamHeight / 2)
        }
    }

    enum Onboarding {
        /// How often the permissions window rereads Screen Recording access.
        static let pollInterval: Duration = .seconds(1)
    }

    enum Logs {
        /// Copy diagnostics includes this many of the newest log lines.
        static let diagnosticsLines = 300
        /// How far back an exported log reaches (the current run only: the store is per process).
        static let exportWindow: TimeInterval = 24 * 60 * 60
    }

    enum Connection {
        /// Pause before sharing listens again after a session ended.
        static let relistenDelay: Duration = .seconds(1)
        /// After a role swap, how long viewing waits for the other computer to advertise itself.
        static let swapReconnectTimeout: Duration = .seconds(20)
        /// Pause before a dropped viewer tries its host again (an old advertisement can refuse at
        /// once; this keeps that from becoming a tight loop).
        static let reconnectDelay: Duration = .seconds(2)
        /// Capabilities this Mac announces in either role.
        static let maximumWidth = 7680
        static let maximumHeight = 4320
        static let maximumRefreshHz = 240
    }

    enum KeyStore {
        static let keychainService = "com.akinalpfdn.osplay"
        static let identityAccount = "identity"
        static let peersAccount = "paired-devices"
        /// Shown in Keychain Access.
        static let itemLabel = "Osplay"
    }

    enum Host {
        /// Virtual displays offer HiDPI "looks like" sizes; per-peer choice arrives in Phase 11.
        static let defaultHiDPI = true
        /// Viewer monitors scaled at least this much start in the crisp HiDPI resolution; others
        /// start at one point per pixel.
        static let hiDPIMinimumScalePercent = 150
        /// Streams never exceed this rate; 120 Hz capture runs at ~80 fps on a MacBook Air M3.
        static let maximumRefreshMillihertz: UInt32 = 60_000
    }

    enum Capture {
        /// Buffers ScreenCaptureKit may have in flight; one stays with the encoder and one is kept
        /// as the last frame for keyframe re-sends, so this must leave room for both.
        static let queueDepth = 5
        /// Capture may run this much faster than the stream's refresh rate; the display itself
        /// limits it to real changes, so this only absorbs timing jitter.
        static let frameRateHeadroom = 2.0
        /// A new virtual display appears in ScreenCaptureKit's display list shortly after creation.
        static let displayLookupTimeout: Duration = .seconds(3)
        static let displayLookupInterval: Duration = .milliseconds(100)
    }

    #if DEBUG
        enum Developer {
            /// Peer ID for test displays, so their arrangement persists between debug sessions.
            static let testPeerID = Data("osplay-developer-test-peer".utf8)
            static let testRefreshRate = 60.0
            /// 27-inch 16:9 panel; only influences which sizes macOS recommends.
            static let testPhysicalSizeMillimeters = CGSize(width: 597, height: 336)
            /// Host media test: the monitor the in-process viewer offers (Phase 6 acceptance size).
            static let hostTestMonitor = (width: 2560, height: 1440, refreshMillihertz: UInt32(60_000))
            static let hostTestDeviceName = "Osplay Host Test"
            /// Viewer loopback: the virtual display it hosts and views.
            static let loopbackMonitor = (width: 1920, height: 1080, refreshMillihertz: UInt32(60_000))
            static let loopbackDeviceName = "Osplay Loopback"
            static let freezeDelay: Duration = .seconds(5)
            /// Frame intervals the pacing statistics cover (5 s at 60 fps).
            static let pacingWindow = 300
            static let statisticsRefreshInterval: Duration = .milliseconds(500)
            /// Debug `.h264` dumps go to ~/Movies/Osplay.
            static var dumpDirectory: URL {
                let movies =
                    FileManager.default.urls(for: .moviesDirectory, in: .userDomainMask).first
                    ?? FileManager.default.homeDirectoryForCurrentUser.appending(path: "Movies")
                return movies.appending(path: "Osplay")
            }
            static let testResolutions: [DisplaySize] = [
                DisplaySize(width: 2560, height: 1440),
                DisplaySize(width: 1920, height: 1080),
                DisplaySize(width: 2560, height: 1664),
                DisplaySize(width: 3840, height: 2160),
            ]
        }
    #endif
}
