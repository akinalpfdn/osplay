import CoreBridge
import Foundation
import OSLog

/// The part of a core session the host uses; `CoreSession` in the app, a fake in tests.
protocol HostCoreSession: AnyObject, Sendable {
    var events: AsyncStream<SessionEvent> { get }
    func confirmPairing(_ accepted: Bool) throws(CoreError)
    func requestRoleSwap() throws(CoreError)
    func answerRoleSwap(_ answer: RoleSwapAnswer) throws(CoreError)
    func configureStreams(_ streams: [StreamConfiguration]) throws(CoreError)
    func push(_ unit: AccessUnit) throws(CoreError)
    func close()
    func closeForSleep()
}

extension CoreSession: HostCoreSession {}

/// What the host applies when a viewer's monitors arrive; read then, so settings changed while
/// waiting for a viewer take effect.
struct HostDisplayPolicy: Sendable {
    /// The viewer's identity; virtual displays derive theirs from it so macOS remembers the
    /// arrangement per viewer.
    var peerID: Data
    var hiDPI: HiDPIChoice = .automatic
    /// The last arrangement of this viewer's monitors (empty: a row to the right).
    var arrangement: [ArrangedMonitor] = []
    var quality: QualityPreset = .high
}

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
        /// The viewer introduced itself.
        case peer(PeerInfo)
        /// First contact: show the code; answer with `confirmPairing`.
        case pairingCode(UInt32, peerPublicKey: [UInt8])
        /// Both users accepted: store the key as trusted.
        case paired(peerPublicKey: [UInt8])
        /// Streams are configured; each runs on the virtual display with the given ID.
        case streaming([LentDisplay])
        case statistics(SessionStatistics)
        /// The other computer asks to reverse the roles; answer with `answerRoleSwap`.
        case roleSwapRequested(peerPublicKey: [UInt8])
        /// The answer to `requestRoleSwap`; after `.accepted` the session closes.
        case roleSwapAnswered(RoleSwapAnswer, peerPublicKey: [UInt8])
    }

    nonisolated let events: AsyncStream<Event>
    private let eventSink: AsyncStream<Event>.Continuation
    private let core: any HostCoreSession
    private let displays: any DisplayLending
    private let captures: any ScreenCaptureMaking
    private let encoders: any VideoEncoderMaking
    private let permissions: any PermissionChecking
    private let policy: @Sendable () async -> HostDisplayPolicy
    private let dump: StreamDump?
    private var pipelines: [UInt8: StreamPipeline] = [:]
    private var failure: HostSessionError?

    init(
        core: any HostCoreSession, policy: @escaping @Sendable () async -> HostDisplayPolicy,
        displays: any DisplayLending,
        captures: any ScreenCaptureMaking, encoders: any VideoEncoderMaking, permissions: any PermissionChecking,
        dumpDirectory: URL? = nil
    ) {
        self.core = core
        self.policy = policy
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
            case .bitrateTarget(let streamID, let kbps):
                Log.host.info("Stream \(streamID) bitrate target \(kbps) kbps")
                pipelines[streamID]?.setBitrate(kbps: kbps)
            case .streamStopped(let streamID):
                await stopStream(streamID)
            case .state(let state, let reason):
                eventSink.yield(.state(state))
                if state == .closed {
                    await stopStreams()
                    core.close()  // releases the core session also after closeForSleep
                    // A viewer leaving or going to sleep is a normal end for a host.
                    if failure == nil, let reason, reason != .connectionClosed, reason != .peerSleeping {
                        failure = .session(reason)
                    }
                    return failure
                }
            case .statistics(let statistics):
                eventSink.yield(.statistics(statistics))
            case .log(let level, let message):
                log(level, message)
            case .peer(let peer):
                eventSink.yield(.peer(peer))
            case .pairingCode(let code, let key):
                eventSink.yield(.pairingCode(code, peerPublicKey: key))
            case .paired(let key):
                eventSink.yield(.paired(peerPublicKey: key))
            case .roleSwapRequested(let key):
                eventSink.yield(.roleSwapRequested(peerPublicKey: key))
            case .roleSwapAnswered(let answer, let key):
                eventSink.yield(.roleSwapAnswered(answer, peerPublicKey: key))
            case .streamConfiguration, .accessUnit, .cursorShape, .cursorPosition:
                break  // viewer-only
            }
        }
        await stopStreams()
        return failure
    }

    /// Ends the session with Bye; `run` returns once the core reports `.closed`.
    func stop() {
        core.close()
    }

    /// Ends the session telling the other computer this Mac is going to sleep (it waits for it).
    func stopForSleep() {
        core.closeForSleep()
    }

    /// Answers `.pairingCode` with the local user's decision.
    func confirmPairing(_ accepted: Bool) {
        do throws(CoreError) {
            try core.confirmPairing(accepted)
        } catch {
            Log.host.error("Pairing answer failed: \(String(describing: error), privacy: .public)")
        }
    }

    /// Asks the other computer to reverse the roles; throws when it cannot be asked now.
    func requestRoleSwap() throws(CoreError) {
        try core.requestRoleSwap()
    }

    /// Answers `.roleSwapRequested`; `.accepted` closes the session.
    func answerRoleSwap(_ answer: RoleSwapAnswer) {
        do throws(CoreError) {
            try core.answerRoleSwap(answer)
        } catch {
            Log.host.error("Role swap answer failed: \(String(describing: error), privacy: .public)")
        }
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
        let offered = monitors.prefix(Tunables.VirtualDisplay.maximumDisplayCount).map(Self.capped)
        let policy = await policy()
        let lent: [LentDisplay]
        do {
            lent = try await displays.lend(
                offered, peerID: policy.peerID, hiDPI: policy.hiDPI, arrangement: policy.arrangement)
        } catch {
            end(with: .display(error))
            return
        }
        let streams = lent.enumerated().map { index, display in
            Self.streamConfiguration(id: UInt8(index), for: display.monitor, quality: policy.quality)
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

    /// The viewer dropped one stream (e.g. its monitor was unplugged): only that display goes.
    private func stopStream(_ streamID: UInt8) async {
        guard let pipeline = pipelines.removeValue(forKey: streamID) else { return }
        await pipeline.stop()
        await displays.returnDisplay(pipeline.displayID)
    }

    private func stopStreams() async {
        let running = pipelines.keys.sorted().compactMap { pipelines[$0] }
        pipelines.removeAll()
        for pipeline in running {
            await pipeline.stop()
        }
        await displays.returnAll()
    }

    /// High refresh streams come after the MVP (DEVPLAN): a 120 Hz monitor gets a 60 Hz display and
    /// stream, which this Mac captures and encodes at full rate.
    static func capped(_ monitor: MonitorDescription) -> MonitorDescription {
        MonitorDescription(
            id: monitor.id, widthPixels: monitor.widthPixels, heightPixels: monitor.heightPixels,
            refreshMillihertz: min(monitor.refreshMillihertz, Tunables.Host.maximumRefreshMillihertz),
            physicalWidthMillimeters: monitor.physicalWidthMillimeters,
            physicalHeightMillimeters: monitor.physicalHeightMillimeters, scalePercent: monitor.scalePercent,
            name: monitor.name)
    }

    static func streamConfiguration(
        id: UInt8, for monitor: MonitorDescription, quality: QualityPreset
    )
        -> StreamConfiguration
    {
        StreamConfiguration(
            streamID: id, monitorID: monitor.id, width: monitor.widthPixels, height: monitor.heightPixels,
            refreshMillihertz: monitor.refreshMillihertz, profile: Tunables.Encoding.profile, cursorMode: .inVideo,
            bitrateKbps: quality.bitrateKbps(
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
