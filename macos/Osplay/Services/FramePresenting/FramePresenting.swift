import CoreBridge
import CoreGraphics

/// Where a stream is shown.
enum PresentationTarget: Hashable, Sendable {
    /// A borderless window covering this display; menu bar and Dock are hidden.
    case fullScreen(CGDirectDisplayID)
    /// An ordinary window (debug loopback).
    case window(title: String)
}

/// Accepts decoded frames from the decoder's thread. Kept apart from the presenter so the frame
/// path never waits for the main actor.
protocol FrameSink: AnyObject, Sendable {
    func enqueue(_ frame: DecodedFrame)
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
}

/// Creates presenters; injected so the viewer session can be tested without windows.
@MainActor
protocol FramePresenterMaking: AnyObject, Sendable {
    func makePresenter(for stream: StreamConfiguration, target: PresentationTarget) -> any FramePresenting
}
