import CoreBridge
import Foundation
import OSLog

/// The part of a core session the viewer uses; `CoreSession` in the app, a fake in tests.
protocol ViewerCoreSession: AnyObject, Sendable {
    var events: AsyncStream<SessionEvent> { get }
    func requestKeyframe(_ streamID: UInt8) throws(CoreError)
    func pauseStream(_ streamID: UInt8) throws(CoreError)
    func resumeStream(_ streamID: UInt8) throws(CoreError)
    func close()
}

extension CoreSession: ViewerCoreSession {}

/// Owns one viewer session: the core session, a decoder and a presenter per stream, and the power
/// assertion while frames are on screen (DEVPLAN Architecture, `Features/Viewer`).
actor ViewerSession {
    enum Event: Sendable {
        case state(SessionState)
        case streamAdded(StreamConfiguration)
        case visibility(Bool)
        case statistics(SessionStatistics)
    }

    nonisolated let events: AsyncStream<Event>
    private let eventSink: AsyncStream<Event>.Continuation
    private let core: any ViewerCoreSession
    private let decoders: any VideoDecoderMaking
    private let presenters: any FramePresenterMaking
    private let power: any PowerAssertionManaging
    private let target: @Sendable (StreamConfiguration) -> PresentationTarget
    private var streams: [UInt8: (pipeline: ViewerStreamPipeline, presenter: any FramePresenting)] = [:]
    private var visible = true
    private var assertion: (any PowerAssertion)?
    private var failure: CoreError?

    init(
        core: any ViewerCoreSession, decoders: any VideoDecoderMaking, presenters: any FramePresenterMaking,
        power: any PowerAssertionManaging, target: @escaping @Sendable (StreamConfiguration) -> PresentationTarget
    ) {
        self.core = core
        self.decoders = decoders
        self.presenters = presenters
        self.power = power
        self.target = target
        (events, eventSink) = AsyncStream.makeStream(of: Event.self)
    }

    /// Runs until the session closes; returns the reason when it was an error.
    func run() async -> CoreError? {
        defer { eventSink.finish() }
        for await event in core.events {
            switch event {
            case .streamConfiguration(let stream):
                await add(stream)
            case .accessUnit(let unit):
                streams[unit.streamID]?.pipeline.decode(unit)
            case .cursorShape(let shape):
                if let presenter = streams[shape.streamID]?.presenter {
                    await presenter.setCursorShape(shape)
                }
            case .cursorPosition(let position):
                if let presenter = streams[position.streamID]?.presenter {
                    await presenter.moveCursor(position)
                }
            case .state(let state, let reason):
                eventSink.yield(.state(state))
                if state == .closed {
                    await tearDown()
                    if let reason, reason != .connectionClosed {
                        failure = reason
                    }
                    return failure
                }
            case .statistics(let statistics):
                eventSink.yield(.statistics(statistics))
            case .log(let level, let message):
                log(level, message)
            case .streamStopped(let streamID):
                if let entry = streams.removeValue(forKey: streamID) {
                    entry.pipeline.stop()
                    await entry.presenter.close()
                    updatePowerAssertion()
                }
            case .peer, .pairingCode, .paired, .viewerMonitors, .keyframeRequested:
                break  // pairing UI arrives in Phase 10; the rest is host-only
            }
        }
        await tearDown()
        return failure
    }

    /// Hides or shows every stream. Hidden streams are paused, so the host stops sending.
    func setVisible(_ visible: Bool) async {
        guard visible != self.visible else { return }
        self.visible = visible
        for streamID in streams.keys.sorted() {
            guard let entry = streams[streamID] else { continue }
            do throws(CoreError) {
                try visible ? core.resumeStream(streamID) : core.pauseStream(streamID)
            } catch {
                Log.viewer.error(
                    "Pause/resume of stream \(streamID) failed: \(String(describing: error), privacy: .public)")
            }
            if visible {
                await entry.presenter.show()
            } else {
                await entry.presenter.hide()
            }
        }
        updatePowerAssertion()
        eventSink.yield(.visibility(visible))
    }

    func toggleVisibility() async {
        await setVisible(!visible)
    }

    var isVisible: Bool { visible }

    /// Ends the session with Bye; `run` returns once the core reports `.closed`.
    func stop() {
        core.close()
    }

    func statistics() -> [UInt8: ViewerStreamStatistics] {
        streams.mapValues(\.pipeline.currentStatistics)
    }

    /// Debug: damages the next access unit of every stream to exercise recovery.
    func corruptNextFrames() {
        streams.values.forEach { $0.pipeline.corruptNextFrame() }
    }

    private func add(_ stream: StreamConfiguration) async {
        if let existing = streams.removeValue(forKey: stream.streamID) {
            existing.pipeline.stop()
            await existing.presenter.close()
        }
        let presenter = await presenters.makePresenter(for: stream, target: target(stream))
        let pipeline = ViewerStreamPipeline(
            stream: stream, decoders: decoders, sink: presenter.sink,
            needsKeyframe: { [weak self] streamID in
                Task { await self?.requestKeyframe(streamID) }
            })
        streams[stream.streamID] = (pipeline, presenter)
        if visible {
            await presenter.show()
        }
        updatePowerAssertion()
        eventSink.yield(.streamAdded(stream))
    }

    private func requestKeyframe(_ streamID: UInt8) {
        do throws(CoreError) {
            try core.requestKeyframe(streamID)
        } catch {
            Log.viewer.info(
                "Keyframe request for stream \(streamID) not sent: \(String(describing: error), privacy: .public)")
        }
    }

    private func updatePowerAssertion() {
        let needed = visible && !streams.isEmpty
        if needed, assertion == nil {
            assertion = power.preventDisplaySleep(reason: Tunables.Viewer.powerAssertionReason)
        } else if !needed, let current = assertion {
            current.release()
            assertion = nil
        }
    }

    private func tearDown() async {
        let entries = streams.keys.sorted().compactMap { streams[$0] }
        streams.removeAll()
        for entry in entries {
            entry.pipeline.stop()
            await entry.presenter.close()
        }
        updatePowerAssertion()
    }

    private func log(_ level: SessionLogLevel, _ message: String) {
        switch level {
        case .debug: Log.viewer.debug("core: \(message, privacy: .public)")
        case .info: Log.viewer.info("core: \(message, privacy: .public)")
        case .warning: Log.viewer.warning("core: \(message, privacy: .public)")
        case .error: Log.viewer.error("core: \(message, privacy: .public)")
        }
    }
}
