import CoreBridge
import Foundation
import Testing

@Suite("Core session over loopback")
struct CoreSessionTests {
    private let capabilities = SessionCapabilities(
        maximumStreams: 2, maximumWidth: 7680, maximumHeight: 4320, maximumRefreshHz: 240)
    private let monitor = MonitorDescription(
        id: 0, widthPixels: 2560, heightPixels: 1440, refreshMillihertz: 60_000, physicalWidthMillimeters: 597,
        physicalHeightMillimeters: 336, scalePercent: 100, name: "Test Monitor")

    @Test("Pairing, stream setup and access units reach the viewer intact")
    func pairsAndStreams() async throws {
        let host = try CoreSession.host(
            device: SessionDevice(identity: try .generate(), name: "Swift Host", capabilities: capabilities),
            trust: { _ in .unknown })
        let viewer = try CoreSession.viewer(
            device: SessionDevice(identity: try .generate(), name: "Swift Viewer", capabilities: capabilities),
            monitors: [monitor], host: "127.0.0.1", port: host.controlPort, trust: { _ in .unknown })

        let hostEvents = EventRecorder(host.events)
        let viewerEvents = EventRecorder(viewer.events)

        let hostCode = try #require(
            await hostEvents.next { event in
                if case .pairingCode(let code, _) = event { code } else { nil }
            })
        let viewerCode = try #require(
            await viewerEvents.next { event in
                if case .pairingCode(let code, _) = event { code } else { nil }
            })
        #expect(hostCode == viewerCode)

        try host.confirmPairing(true)
        try viewer.confirmPairing(true)
        let offered = try #require(
            await hostEvents.next { event in
                if case .viewerMonitors(let monitors) = event { monitors } else { nil }
            })
        #expect(offered == [monitor])

        let stream = StreamConfiguration(
            streamID: 0, monitorID: 0, width: 2560, height: 1440, refreshMillihertz: 60_000, profile: .high,
            cursorMode: .inVideo, bitrateKbps: 30_000)
        try host.configureStreams([stream])
        let announced = try #require(
            await viewerEvents.next { event in
                if case .streamConfiguration(let configuration) = event { configuration } else { nil }
            })
        #expect(announced == stream)
        _ = await viewerEvents.next { event in
            if case .state(.streaming, _) = event { true } else { nil }
        }

        let payloads = (0..<10).map { index in Data((0..<5000).map { UInt8(truncatingIfNeeded: $0 * 7 + index) }) }
        for (index, payload) in payloads.enumerated() {
            try host.push(
                AccessUnit(
                    streamID: 0, isKeyframe: index == 0, carriesParameterSets: index == 0,
                    captureTimeMicroseconds: UInt64(index), data: payload))
        }
        var received: [Data] = []
        while received.count < payloads.count {
            let unit = try #require(
                await viewerEvents.next { event in
                    if case .accessUnit(let unit) = event { unit } else { nil }
                })
            received.append(unit.data)
        }
        #expect(received == payloads)

        viewer.close()
        let reason = await hostEvents.next { event in
            if case .state(.closed, let reason) = event { reason } else { nil }
        }
        #expect(reason == .connectionClosed)
        host.close()
        #expect(throws: CoreError.invalidState) { try host.confirmPairing(true) }
    }

    @Test("Blocked peers are refused with a specific reason")
    func refusesBlockedPeers() async throws {
        let host = try CoreSession.host(
            device: SessionDevice(identity: try .generate(), name: "Strict Host", capabilities: capabilities),
            trust: { _ in .blocked })
        let viewer = try CoreSession.viewer(
            device: SessionDevice(identity: try .generate(), name: "Viewer", capabilities: capabilities),
            monitors: [monitor], host: "127.0.0.1", port: host.controlPort, trust: { _ in .trusted })
        let viewerEvents = EventRecorder(viewer.events)
        let reason = await viewerEvents.next { event in
            if case .state(.closed, let reason) = event { reason } else { nil }
        }
        #expect(reason == .notTrusted)
    }

    @Test("Invalid configurations fail before anything starts")
    func rejectsInvalidConfiguration() throws {
        let device = SessionDevice(identity: try .generate(), name: "Viewer", capabilities: capabilities)
        #expect(throws: CoreError.invalidArgument) {
            try CoreSession.viewer(device: device, monitors: [], host: "127.0.0.1", port: 1, trust: { _ in .trusted })
        }
        let badName = SessionDevice(identity: try .generate(), name: "Bad\nName", capabilities: capabilities)
        #expect(throws: CoreError.invalidArgument) { try CoreSession.host(device: badName, trust: { _ in .trusted }) }
    }
}

/// Records a session's events on its own task so waits can time out even when nothing arrives.
private final class EventRecorder: @unchecked Sendable {
    private let lock = NSLock()
    private var events: [SessionEvent] = []
    private var cursor = 0
    private var task: Task<Void, Never>?

    init(_ stream: AsyncStream<SessionEvent>) {
        task = Task { [weak self] in
            for await event in stream {
                self?.append(event)
            }
        }
    }

    deinit { task?.cancel() }

    private func append(_ event: SessionEvent) {
        lock.withLock { events.append(event) }
    }

    /// The first not yet consumed event `match` accepts; `nil` after 5 s.
    func next<T>(_ match: (SessionEvent) -> T?) async -> T? {
        let deadline = ContinuousClock.now + .seconds(5)
        while ContinuousClock.now < deadline {
            let found: T? = lock.withLock {
                while cursor < events.count {
                    let event = events[cursor]
                    cursor += 1
                    if let value = match(event) { return value }
                }
                return nil
            }
            if let found { return found }
            try? await Task.sleep(for: .milliseconds(10))  // cancellation ends the wait early
        }
        return nil
    }
}
