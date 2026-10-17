#if DEBUG
    import AppKit
    import CoreBridge
    import Foundation
    import Observation

    /// Debug-only host media test (DEVPLAN Phase 6): a real host session with ScreenCaptureKit and
    /// VideoToolbox, streamed over loopback to an in-process viewer that only measures what arrives.
    @MainActor
    @Observable
    final class DeveloperHostTestViewModel {
        struct StreamRow: Identifiable, Equatable {
            let id: UInt8
            let size: String
            let pacing: PacingMeter.Summary?
            let bitrateKbps: Int
            let latencyMilliseconds: Double?
        }

        var streamCount = 2
        var dumpsEnabled = true
        private(set) var isRunning = false
        private(set) var rows: [StreamRow] = []
        private(set) var errorMessage: String?
        private(set) var needsScreenRecordingPermission = false
        private(set) var dumpFiles: [URL] = []

        private let provider: any VirtualDisplayProviding
        private let captures: any ScreenCaptureMaking
        private let encoders: any VideoEncoderMaking
        private let permissions: any PermissionChecking
        private let patterns = TestPatternWindows()
        private var session: HostSession?
        private var viewer: CoreSession?
        private var tasks: [Task<Void, Never>] = []
        private var meters: [UInt8: PacingMeter] = [:]
        private var streams: [UInt8: StreamConfiguration] = [:]
        private var bitrates: [UInt8: Int] = [:]
        private var latencies: [UInt8: Double] = [:]

        init(
            provider: any VirtualDisplayProviding, captures: any ScreenCaptureMaking, encoders: any VideoEncoderMaking,
            permissions: any PermissionChecking
        ) {
            self.provider = provider
            self.captures = captures
            self.encoders = encoders
            self.permissions = permissions
        }

        func start() {
            guard !isRunning else { return }
            errorMessage = nil
            dumpFiles = []
            needsScreenRecordingPermission = false
            guard permissions.hasScreenRecordingAccess || permissions.requestScreenRecordingAccess() else {
                show(.screenRecordingPermissionMissing)
                return
            }
            do throws(CoreError) {
                try startSessions()
                isRunning = true
            } catch {
                show(.session(error))
            }
        }

        func stop() {
            Task { [session] in await session?.stop() }
        }

        func openScreenRecordingSettings() {
            guard let url = permissions.screenRecordingSettingsURL else { return }
            NSWorkspace.shared.open(url)
        }

        func revealDumps() {
            NSWorkspace.shared.activateFileViewerSelecting(dumpFiles)
        }

        private func startSessions() throws(CoreError) {
            let capabilities = SessionCapabilities(
                maximumStreams: Tunables.VirtualDisplay.maximumDisplayCount, maximumWidth: 7680, maximumHeight: 4320,
                maximumRefreshHz: 240)
            let host = try CoreSession.host(
                device: SessionDevice(
                    identity: try .generate(), name: Tunables.Developer.hostTestDeviceName, capabilities: capabilities),
                trust: { _ in .trusted })
            let viewer = try CoreSession.viewer(
                device: SessionDevice(
                    identity: try .generate(), name: Tunables.Developer.hostTestDeviceName, capabilities: capabilities),
                monitors: testMonitors(), host: "127.0.0.1", port: host.controlPort, trust: { _ in .trusted })
            let session = HostSession(
                core: host, peerID: Tunables.Developer.testPeerID, displays: VirtualDisplayLender(provider: provider),
                captures: captures, encoders: encoders, permissions: permissions,
                dumpDirectory: dumpsEnabled ? Tunables.Developer.dumpDirectory : nil)
            self.session = session
            self.viewer = viewer
            meters = [:]
            streams = [:]
            bitrates = [:]
            latencies = [:]
            rows = []

            tasks = [
                Task { [weak self] in
                    let failure = await session.run()
                    let files = await session.dumpFiles()
                    self?.finish(failure: failure, dumpFiles: files)
                },
                Task { [weak self] in
                    for await event in session.events {
                        self?.handleHost(event)
                    }
                },
                Task { [weak self] in
                    for await event in viewer.events {
                        self?.handleViewer(event)
                    }
                },
                Task { [weak self] in
                    while !Task.isCancelled {
                        // Cancellation ends the loop through the condition above.
                        try? await Task.sleep(for: Tunables.Developer.statisticsRefreshInterval)
                        self?.refreshRows()
                    }
                },
            ]
        }

        private func testMonitors() -> [MonitorDescription] {
            let size = Tunables.Developer.hostTestMonitor
            let millimeters = Tunables.Developer.testPhysicalSizeMillimeters
            return (0..<streamCount).map { index in
                MonitorDescription(
                    id: UInt8(index), widthPixels: size.width, heightPixels: size.height,
                    refreshMillihertz: size.refreshMillihertz, physicalWidthMillimeters: Int(millimeters.width),
                    physicalHeightMillimeters: Int(millimeters.height), scalePercent: 100,
                    name: String(localized: "developer.displayName \(index + 1)"))
            }
        }

        private func handleHost(_ event: HostSession.Event) {
            switch event {
            case .streaming(let displays):
                patterns.show(on: displays.map(\.displayID))
            case .statistics(let statistics):
                for stream in statistics.streams {
                    bitrates[stream.streamID] = stream.bitrateKbps
                }
            case .state:
                break
            }
        }

        private func handleViewer(_ event: SessionEvent) {
            switch event {
            case .streamConfiguration(let stream):
                streams[stream.streamID] = stream
            case .accessUnit(let unit):
                meters[unit.streamID, default: PacingMeter()].record(
                    arrivalAt: Double(DispatchTime.now().uptimeNanoseconds) / 1_000_000_000)
            case .statistics(let statistics):
                for stream in statistics.streams where stream.latencyMicroseconds > 0 {
                    latencies[stream.streamID] = Double(stream.latencyMicroseconds) / 1000
                }
            default:
                break
            }
        }

        private func refreshRows() {
            rows = streams.keys.sorted().compactMap { id in
                guard let stream = streams[id] else { return nil }
                return StreamRow(
                    id: id, size: "\(stream.width)×\(stream.height)", pacing: meters[id]?.summary,
                    bitrateKbps: bitrates[id] ?? 0, latencyMilliseconds: latencies[id])
            }
        }

        private func finish(failure: HostSessionError?, dumpFiles: [URL]) {
            patterns.close()
            viewer?.close()
            viewer = nil
            session = nil
            tasks.forEach { $0.cancel() }
            tasks = []
            isRunning = false
            self.dumpFiles = dumpFiles
            if let failure { show(failure) }
        }

        private func show(_ error: HostSessionError) {
            errorMessage = error.message
            needsScreenRecordingPermission = error.needsScreenRecordingPermission
        }
    }
#endif
