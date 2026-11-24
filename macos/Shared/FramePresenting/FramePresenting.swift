import CoreBridge
import CoreGraphics
import Foundation

/// Where a stream is shown.
enum PresentationTarget: Hashable, Sendable {
    #if os(macOS)
        /// A borderless window covering this display; menu bar and Dock are hidden.
        case fullScreen(CGDirectDisplayID)
        /// An ordinary window (debug loopback).
        case window(title: String)
    #else
        /// The app's full-screen viewer, on the device's only screen.
        case screen
    #endif
}

/// Accepts decoded frames from the decoder's thread. Kept apart from the presenter so the frame
/// path never waits for the main actor.
protocol FrameSink: AnyObject, Sendable {
    func enqueue(_ frame: DecodedFrame)
}

/// What the stats overlay shows for one stream (DEVPLAN: fps, bitrate, RTT, loss, latency).
struct StatsFigures: Equatable, Sendable {
    let framesPerSecond: Int
    let bitrateKbps: Int
    let roundTripMilliseconds: Double
    let lossPercent: Double
    /// Capture to delivery; nil until the clocks are compared.
    let latencyMilliseconds: Double?
    /// The host's sound, on one stream's overlay only.
    let audio: AudioStatistics?

    init(stream: StreamStatistics, roundTripMicroseconds: Int, audio: AudioStatistics? = nil) {
        self.audio = audio
        framesPerSecond = stream.framesPerSecond
        bitrateKbps = stream.bitrateKbps
        roundTripMilliseconds = Double(roundTripMicroseconds) / 1000
        lossPercent = Double(stream.lossPermille) / 10
        latencyMilliseconds = stream.latencyMicroseconds > 0 ? Double(stream.latencyMicroseconds) / 1000 : nil
    }
}

extension StatsFigures {
    /// "60 fps · 22.1 Mbit/s · RTT 4 ms · loss 0.0% · latency 12 ms", and while sound plays a second
    /// line: "sound: reserve 40 ms · latency 55 ms · gaps 0 · concealed 0".
    var overlayText: String {
        guard let audio else { return streamLine }
        let reserve = (Double(audio.bufferedMicroseconds) / 1000).formatted(.number.precision(.fractionLength(0)))
        let latency =
            audio.latencyMicroseconds > 0
            ? (Double(audio.latencyMicroseconds) / 1000).formatted(.number.precision(.fractionLength(0))) : "–"
        let sound = String(
            localized: "stats.overlaySound \(reserve) \(latency) \(Int(audio.underruns)) \(Int(audio.concealedFrames))")
        return streamLine + "\n" + sound
    }

    private var streamLine: String {
        let bitrate = (Double(bitrateKbps) / 1000).formatted(.number.precision(.fractionLength(1)))
        let roundTrip = roundTripMilliseconds.formatted(.number.precision(.fractionLength(0)))
        let loss = lossPercent.formatted(.number.precision(.fractionLength(1)))
        let latency = latencyMilliseconds?.formatted(.number.precision(.fractionLength(0))) ?? "–"
        return String(localized: "stats.overlay \(framesPerSecond) \(bitrate) \(roundTrip) \(loss) \(latency)")
    }
}

/// Shows one stream: its video, scaled to fit, and for separate-cursor streams the cursor.
@MainActor
protocol FramePresenting: AnyObject, Sendable {
    nonisolated var sink: any FrameSink { get }
    func show()
    func hide()
    func close()
    func setCursorShape(_ shape: CursorShape)
    func moveCursor(_ position: CursorPosition)
    /// Shows the stats overlay with these figures, or hides it for nil.
    func showStats(_ figures: StatsFigures?)
}

/// Creates presenters; injected so the viewer session can be tested without windows.
@MainActor
protocol FramePresenterMaking: AnyObject, Sendable {
    func makePresenter(for stream: StreamConfiguration, target: PresentationTarget) -> any FramePresenting
}
