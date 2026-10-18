import AVFoundation
import AppKit
import CoreBridge
import OSLog

/// Presents a stream with `AVSampleBufferDisplayLayer`: every decoded frame is shown immediately,
/// with no buffering for smoothness. A cursor layer on top draws separate-cursor streams.
@MainActor
final class SampleBufferPresenter: NSObject, FramePresenting {
    nonisolated let sink: any FrameSink
    private let stream: StreamConfiguration
    private let target: PresentationTarget
    private let window: NSWindow
    private let content: PresenterView
    private var cursorShape: CursorShape?
    private var cursorPosition: CursorPosition?
    private var idleTimer: Timer?

    init(stream: StreamConfiguration, target: PresentationTarget) {
        self.stream = stream
        self.target = target
        content = PresenterView(streamSize: CGSize(width: stream.width, height: stream.height))
        sink = RendererSink(renderer: content.displayLayer.sampleBufferRenderer)
        window = Self.makeWindow(target: target, stream: stream)
        super.init()
        window.contentView = content
    }

    func show() {
        if case .fullScreen = target {
            NSApp.activate()
            window.makeKeyAndOrderFront(nil)
            restartIdleTimer()
        } else {
            window.makeKeyAndOrderFront(nil)
        }
    }

    func hide() {
        idleTimer?.invalidate()
        window.orderOut(nil)
    }

    func close() {
        idleTimer?.invalidate()
        content.displayLayer.sampleBufferRenderer.flush(removingDisplayedImage: true, completionHandler: nil)
        window.close()
    }

    func setCursorShape(_ shape: CursorShape) {
        cursorShape = shape
        content.cursorLayer.contents = Self.cursorImage(shape)
        layoutCursor()
    }

    func moveCursor(_ position: CursorPosition) {
        cursorPosition = position
        layoutCursor()
    }

    private func layoutCursor() {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        guard let shape = cursorShape, let position = cursorPosition, position.isVisible,
            position.shapeID == shape.shapeID,
            let frame = CursorGeometry.frame(
                position: position, shape: shape, streamSize: content.streamSize, bounds: content.bounds)
        else {
            content.cursorLayer.isHidden = true
            return
        }
        content.cursorLayer.isHidden = false
        // CursorGeometry works top-down like the stream; layers count from the bottom.
        content.cursorLayer.frame = CGRect(
            x: frame.minX, y: content.bounds.height - frame.maxY, width: frame.width, height: frame.height)
    }

    /// The local pointer disappears after a short idle time over a full-screen viewer.
    private func restartIdleTimer() {
        idleTimer?.invalidate()
        idleTimer = Timer.scheduledTimer(withTimeInterval: Tunables.Viewer.cursorIdleDelay, repeats: false) { _ in
            MainActor.assumeIsolated { NSCursor.setHiddenUntilMouseMoves(true) }
        }
        content.onMouseMoved = { [weak self] in self?.restartIdleTimer() }
    }

    private static func makeWindow(target: PresentationTarget, stream: StreamConfiguration) -> NSWindow {
        switch target {
        case .fullScreen(let displayID):
            let screen = NSScreen.screens.first { $0.displayID == displayID } ?? NSScreen.main
            let frame = screen?.frame ?? .zero
            let window = NSWindow(
                contentRect: frame, styleMask: .borderless, backing: .buffered, defer: false, screen: screen)
            // Above the menu bar and the Dock of that screen, on every Space.
            window.level = .init(rawValue: NSWindow.Level.mainMenu.rawValue + 1)
            window.collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle, .fullScreenAuxiliary]
            window.backgroundColor = NSColor(Theme.Viewer.background)
            window.setFrame(frame, display: false)
            window.acceptsMouseMovedEvents = true
            window.isReleasedWhenClosed = false
            return window
        case .window(let title):
            let size = Tunables.Viewer.loopbackWindowSize(streamWidth: stream.width, streamHeight: stream.height)
            let window = NSWindow(
                contentRect: CGRect(origin: .zero, size: size),
                styleMask: [.titled, .closable, .resizable, .miniaturizable], backing: .buffered, defer: false)
            window.title = title
            window.contentAspectRatio = CGSize(width: stream.width, height: stream.height)
            window.backgroundColor = NSColor(Theme.Viewer.background)
            window.isReleasedWhenClosed = false
            window.center()
            return window
        }
    }

    private static func cursorImage(_ shape: CursorShape) -> CGImage? {
        guard let provider = CGDataProvider(data: shape.bgra as CFData) else { return nil }
        return CGImage(
            width: shape.width, height: shape.height, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: shape.width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB) ?? CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(
                rawValue: CGBitmapInfo.byteOrder32Little.rawValue | CGImageAlphaInfo.premultipliedFirst.rawValue),
            provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}

/// Maps a cursor position in stream pixels onto the aspect-fitted video inside a view, in
/// top-down coordinates.
enum CursorGeometry {
    static func frame(position: CursorPosition, shape: CursorShape, streamSize: CGSize, bounds: CGRect) -> CGRect? {
        guard streamSize.width > 0, streamSize.height > 0, bounds.width > 0, bounds.height > 0 else { return nil }
        let video = AVMakeRect(aspectRatio: streamSize, insideRect: bounds)
        let scale = video.width / streamSize.width
        return CGRect(
            x: video.minX + (CGFloat(position.x) - CGFloat(shape.hotspotX)) * scale,
            y: video.minY + (CGFloat(position.y) - CGFloat(shape.hotspotY)) * scale,
            width: CGFloat(shape.width) * scale, height: CGFloat(shape.height) * scale)
    }
}

/// Hands frames to the layer's renderer from the decoder's thread.
private final class RendererSink: FrameSink, @unchecked Sendable {
    // @unchecked: AVSampleBufferVideoRenderer accepts samples from any thread; one decoder
    // queue calls `enqueue`.
    private let renderer: AVSampleBufferVideoRenderer

    init(renderer: AVSampleBufferVideoRenderer) {
        self.renderer = renderer
    }

    func enqueue(_ frame: DecodedFrame) {
        if renderer.status == .failed {
            Log.viewer.error("Renderer failed: \(String(describing: self.renderer.error), privacy: .public)")
            renderer.flush()
        }
        guard let sample = Self.sampleBuffer(frame) else { return }
        renderer.enqueue(sample)
    }

    private static func sampleBuffer(_ frame: DecodedFrame) -> CMSampleBuffer? {
        var format: CMVideoFormatDescription?
        guard
            CMVideoFormatDescriptionCreateForImageBuffer(
                allocator: nil, imageBuffer: frame.pixelBuffer, formatDescriptionOut: &format) == noErr,
            let format
        else { return nil }
        var timing = CMSampleTimingInfo(
            duration: .invalid,
            presentationTimeStamp: CMTime(value: CMTimeValue(frame.captureTimeMicroseconds), timescale: 1_000_000),
            decodeTimeStamp: .invalid)
        var sample: CMSampleBuffer?
        guard
            CMSampleBufferCreateReadyWithImageBuffer(
                allocator: nil, imageBuffer: frame.pixelBuffer, formatDescription: format, sampleTiming: &timing,
                sampleBufferOut: &sample) == noErr,
            let sample
        else { return nil }
        // Shown as soon as it is enqueued, regardless of its timestamp.
        if let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: true),
            CFArrayGetCount(attachments) > 0
        {
            let dictionary = unsafeBitCast(CFArrayGetValueAtIndex(attachments, 0), to: CFMutableDictionary.self)
            CFDictionarySetValue(
                dictionary, Unmanaged.passUnretained(kCMSampleAttachmentKey_DisplayImmediately).toOpaque(),
                Unmanaged.passUnretained(kCFBooleanTrue).toOpaque())
        }
        return sample
    }
}

/// The window's content: the video layer and the cursor layer above it.
private final class PresenterView: NSView {
    let streamSize: CGSize
    let displayLayer = AVSampleBufferDisplayLayer()
    let cursorLayer = CALayer()
    var onMouseMoved: (() -> Void)?

    init(streamSize: CGSize) {
        self.streamSize = streamSize
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor(Theme.Viewer.background).cgColor
        displayLayer.videoGravity = .resizeAspect
        displayLayer.autoresizingMask = [.layerWidthSizable, .layerHeightSizable]
        cursorLayer.isHidden = true
        cursorLayer.magnificationFilter = .nearest
        layer?.addSublayer(displayLayer)
        layer?.addSublayer(cursorLayer)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { nil }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        displayLayer.frame = bounds
        CATransaction.commit()
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(
            NSTrackingArea(rect: bounds, options: [.mouseMoved, .activeAlways, .inVisibleRect], owner: self))
    }

    override func mouseMoved(with event: NSEvent) {
        onMouseMoved?()
    }
}

/// Creates layer-backed presenters.
@MainActor
final class SampleBufferPresenterFactory: FramePresenterMaking {
    func makePresenter(for stream: StreamConfiguration, target: PresentationTarget) -> any FramePresenting {
        SampleBufferPresenter(stream: stream, target: target)
    }
}
