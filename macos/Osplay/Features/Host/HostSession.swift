import CoreBridge
import Foundation
import OSLog

/// The part of a core session the host uses; `CoreSession` in the app, a fake in tests.
protocol HostCoreSession: AnyObject, Sendable {
    var events: AsyncStream<SessionEvent> { get }
    func configureStreams(_ streams: [StreamConfiguration]) throws(CoreError)
    func push(_ unit: AccessUnit) throws(CoreError)
    func close()
}

extension CoreSession: HostCoreSession {}

/// Why a host session ended other than by an orderly close.
enum HostSessionError: Error, Equatable, Sendable {
    /// Screen Recording is not allowed; nothing was started.
    case screenRecordingPermissionMissing
    case display(VirtualDisplayError)
    case stream(StreamFailure)
    case session(CoreError)
}

/// Owns one host session: the core session, the virtual displays standing in for the viewer's
/// monitors, and a capture → encode pipeline per display (DEVPLAN Architecture, `Features/Host`).
actor HostSession {
    enum Event: Sendable {
        case state(SessionState)
        /// Streams are configured; each runs on the virtual display with the given ID.
        case streaming([LentDisplay])
        case statistics(SessionStatistics)
    }

    nonisolated let events: AsyncStream<Event>
    private let eventSink: AsyncStream<Event>.Continuation
    private let core: any HostCoreSession
    private let displays: any DisplayLending
    private let captures: any ScreenCaptureMaking
    private let encoders: any VideoEncoderMaking
    private let permissions: any PermissionChecking
    private let peerID: Data
    private let dump: StreamDump?
    private var pipelines: [UInt8: StreamPipeline] = [:]
    private var failure: HostSessionError?

    init(
        core: any HostCoreSession, peerID: Data, displays: any DisplayLending, captures: any ScreenCaptureMaking,
        encoders: any VideoEncoderMaking, permissions: any PermissionChecking, dumpDirectory: URL? = nil
    ) {
        self.core = core
        self.peerID = peerID
        self.displays = displays
        self.captures = captures
        self.encoders = encoders
        self.permissions = permissions
        dump = dumpDirectory.map { StreamDump(directory: $0) }
        (events, eventSink) = AsyncStream.makeStream(of: Event.self)
    }

    /// Runs until the session closes. Returns `nil` for an orderly close (Bye from either side).
    func run() async -> HostSessionError? {
        defer { eventSink.finish() }
        guard permissions.hasScreenRecordingAccess else {
            core.close()
            return .screenRecordingPermissionMissing
        }
        for await event in core.events {
            switch event {
            case .viewerMonitors(let monitors):
                await startStreams(for: monitors)
            case .keyframeRequested(let streamID):
                pipelines[streamID]?.requestKeyframe()
            case .state(let state, let reason):
                eventSink.yield(.state(state))
                if state == .closed {
                    await stopStreams()
                    if failure == nil, let reason, reason != .connectionClosed {
                        failure = .session(reason)
                    }
                    return failure
                }
            case .statistics(let statistics):
                eventSink.yield(.statistics(statistics))
            case .log(let level, let message):
                log(level, message)
            case .peer, .pairingCode, .paired, .streamConfiguration, .accessUnit, .cursorShape, .cursorPosition:
                break  // pairing UI arrives in Phase 10; the rest is viewer-only
            }
        }
        await stopStreams()
        return failure
    }

    /// Ends the session with Bye; `run` returns once the core reports `.closed`.
    func stop() {
        core.close()
    }

    /// Frames captured so far per stream, for diagnostics.
    func capturedFrameCounts() -> [UInt8: UInt64] {
        pipelines.mapValues(\.capturedFrameCount)
    }

    /// Files written by the debug dump, once the session has ended.
    func dumpFiles() -> [URL] {
        dump?.finish() ?? []
    }

    private func startStreams(for monitors: [MonitorDescription]) async {
        let offered = Array(monitors.prefix(Tunables.VirtualDisplay.maximumDisplayCount))
        let lent: [LentDisplay]
        do {
            lent = try await displays.lend(offered, peerID: peerID)
        } catch {
            end(with: .display(error))
            return
        }
        let streams = lent.enumerated().map { index, display in
            Self.streamConfiguration(id: UInt8(index), for: display.monitor)
        }
        for (stream, display) in zip(streams, lent) {
            dump?.prepare(stream)
            pipelines[stream.streamID] = makePipeline(stream, displayID: display.displayID)
        }
        do {
            try core.configureStreams(streams)
            for streamID in pipelines.keys.sorted() {
                try await pipelines[streamID]?.start()
            }
        } catch let error as CoreError {
            end(with: .session(error))
            return
        } catch let error as StreamFailure {
            end(with: .stream(error))
            return
        } catch {
            end(with: .session(.internalFailure))
            return
        }
        eventSink.yield(.streaming(lent))
    }

    private func makePipeline(_ stream: StreamConfiguration, displayID: CGDirectDisplayID) -> StreamPipeline {
        let core = core
        let dump = dump
        return StreamPipeline(
            stream: stream, displayID: displayID, captures: captures, encoders: encoders,
            sink: { unit in
                // Fails only once the session is closing; the frame is then of no use.
                try? core.push(unit)
                dump?.write(unit)
            },
            onFailure: { [weak self] failure in
                Task { await self?.end(with: .stream(failure)) }
            })
    }

    /// Records the first failure and closes the session; `run` finishes the teardown.
    private func end(with error: HostSessionError) {
        guard failure == nil else { return }
        failure = error
        Log.host.error("Host session failed: \(String(describing: error), privacy: .public)")
        core.close()
    }

    private func stopStreams() async {
        let running = pipelines.keys.sorted().compactMap { pipelines[$0] }
        pipelines.removeAll()
        for pipeline in running {
            await pipeline.stop()
        }
        await displays.returnAll()
    }

    static func streamConfiguration(id: UInt8, for monitor: MonitorDescription) -> StreamConfiguration {
        StreamConfiguration(
            streamID: id, monitorID: monitor.id, width: monitor.widthPixels, height: monitor.heightPixels,
            refreshMillihertz: monitor.refreshMillihertz, profile: Tunables.Encoding.profile, cursorMode: .inVideo,
            bitrateKbps: Tunables.Encoding.bitrateKbps(
                width: monitor.widthPixels, height: monitor.heightPixels, frameRate: monitor.refreshRate))
    }

    private func log(_ level: SessionLogLevel, _ message: String) {
        switch level {
        case .debug: Log.host.debug("core: \(message, privacy: .public)")
        case .info: Log.host.info("core: \(message, privacy: .public)")
        case .warning: Log.host.warning("core: \(message, privacy: .public)")
        case .error: Log.host.error("core: \(message, privacy: .public)")
        }
    }
}
