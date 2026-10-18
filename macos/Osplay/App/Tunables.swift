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
        /// Bits per pixel per frame at the target bitrate: 0.1 gives ~22 Mbit/s for 1440p60.
        static let bitsPerPixel = 0.1
        static let bitrateRangeKbps = 2_000...80_000
        /// Rate control may exceed the average by this factor within one second.
        static let peakBitrateFactor = 1.5
        /// Safety-net keyframe interval in seconds; keyframes normally come on request.
        static let maximumKeyframeInterval = 60.0
        static let profile = H264Profile.high

        static func bitrateKbps(width: Int, height: Int, frameRate: Double) -> Int {
            let kbps = Int(Double(width * height) * frameRate * bitsPerPixel / 1000)
            return min(max(kbps, bitrateRangeKbps.lowerBound), bitrateRangeKbps.upperBound)
        }
    }

    enum Viewer {
        /// The local pointer hides after this many seconds without movement over a viewer.
        static let cursorIdleDelay: TimeInterval = 2
        /// Decoder errors closer together than this send one keyframe request (the core limits
        /// requests to one per 250 ms as well).
        static let keyframeRequestSpacing: Duration = .milliseconds(100)
        static let powerAssertionReason = "Osplay is showing another computer's display"
        /// The default toggle hotkey (User decision: ⌃⌥⌘V); configurable from Phase 11.
        static let toggleHotkey = Hotkey(keyCode: UInt32(kVK_ANSI_V), modifiers: [.control, .option, .command])

        /// Debug loopback windows open at half the stream's size.
        static func loopbackWindowSize(streamWidth: Int, streamHeight: Int) -> CGSize {
            CGSize(width: streamWidth / 2, height: streamHeight / 2)
        }
    }

    enum Host {
        /// Virtual displays offer HiDPI "looks like" sizes; per-peer choice arrives in Phase 11.
        static let defaultHiDPI = true
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
            static let remoteHostDeviceName = "Osplay Remote Host Test"
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
