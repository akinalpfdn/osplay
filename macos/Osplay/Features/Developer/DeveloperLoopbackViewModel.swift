#if DEBUG
    import AppKit
    import CoreBridge
    import Foundation
    import Observation

    /// Debug-only loopback (DEVPLAN Phase 7): the Mac hosts a virtual display and views it on itself
    /// through the whole pipeline — capture, encode, core, decode, present.
    @MainActor
    @Observable
    final class DeveloperLoopbackViewModel {
        var fullScreen = false
        /// Pauses the host's stream after a few seconds, to check that the hotkey still exits.
        var freezeAfterDelay = false
        private(set) var isRunning = false
        private(set) var framesPerSecond: Double?
        private(set) var decodeErrors: UInt64 = 0
        private(set) var lastRecoveryMilliseconds: Double?
        private(set) var isViewerVisible = true
        private(set) var errorMessage: String?
        private(set) var needsScreenRecordingPermission = false

        private let dependencies: AppDependencies
        private let patterns = TestPatternWindows()
        private var host: HostSession?
        private var hostCore: CoreSession?
        private var viewer: ViewerSession?
        private var hotkey: HotkeyRegistration?
        private var tasks: [Task<Void, Never>] = []
        private var lastPresented: UInt64 = 0
        private var lastSample: ContinuousClock.Instant?

        init(dependencies: AppDependencies) {
            self.dependencies = dependencies
        }

        func start() {
            guard !isRunning else { return }
            errorMessage = nil
            needsScreenRecordingPermission = false
            let permissions = dependencies.permissions
            guard permissions.hasScreenRecordingAccess || permissions.requestScreenRecordingAccess() else {
                show(HostSessionError.screenRecordingPermissionMissing)
                return
            }
            do throws(CoreError) {
                try startSessions()
                isRunning = true
            } catch {
                show(HostSessionError.session(error))
            }
        }

        func stop() {
            Task { [host, viewer] in
                await viewer?.stop()
                await host?.stop()
            }
        }

        func corruptNextFrame() {
            Task { [viewer] in await viewer?.corruptNextFrames() }
        }

        func toggleViewer() {
            Task { [weak self, viewer] in
                await viewer?.toggleVisibility()
                self?.isViewerVisible = await viewer?.isVisible ?? true
            }
        }

        func openScreenRecordingSettings() {
            guard let url = dependencies.permissions.screenRecordingSettingsURL else { return }
            NSWorkspace.shared.open(url)
        }

        private func startSessions() throws(CoreError) {
            let capabilities = SessionCapabilities(
                maximumStreams: 1, maximumWidth: 7680, maximumHeight: 4320, maximumRefreshHz: 240)
            let hostCore = try CoreSession.host(
                device: SessionDevice(
                    identity: try .generate(), name: Tunables.Developer.loopbackDeviceName, capabilities: capabilities),
                trust: { _ in .trusted })
            let viewerCore = try CoreSession.viewer(
                device: SessionDevice(
                    identity: try .generate(), name: Tunables.Developer.loopbackDeviceName, capabilities: capabilities),
                monitors: [loopbackMonitor()], host: "127.0.0.1", port: hostCore.controlPort, trust: { _ in .trusted })
            let host = HostSession(
                core: hostCore, peerID: { Tunables.Developer.testPeerID },
                displays: VirtualDisplayLender(provider: dependencies.virtualDisplays), captures: dependencies.captures,
                encoders: dependencies.encoders, permissions: dependencies.permissions)
            let target: PresentationTarget =
                fullScreen
                ? .fullScreen(Self.builtInDisplayID())
                : .window(title: String(localized: "developer.loopback.windowTitle"))
            let viewer = ViewerSession(
                core: viewerCore, decoders: dependencies.decoders, presenters: dependencies.presenters,
                power: dependencies.power, target: { _ in target })
            self.host = host
            self.hostCore = hostCore
            self.viewer = viewer
            isViewerVisible = true
            framesPerSecond = nil
            decodeErrors = 0
            lastRecoveryMilliseconds = nil
            lastPresented = 0
            lastSample = nil
            registerHotkey()

            let freeze = freezeAfterDelay
            tasks = [
                Task { [weak self] in
                    let failure = await host.run()
                    self?.hostEnded(failure)
                },
                Task { [weak self] in
                    let failure = await viewer.run()
                    self?.viewerEnded(failure)
                },
                Task { [weak self] in
                    for await event in host.events {
                        if case .streaming(let displays) = event {
                            self?.patterns.show(on: displays.map(\.displayID))
                        }
                    }
                },
                Task { [weak self] in
                    while !Task.isCancelled {
                        // Cancellation ends the loop through the condition above.
                        try? await Task.sleep(for: Tunables.Developer.statisticsRefreshInterval)
                        await self?.refresh(viewer.statistics()[0])
                    }
                },
            ]
            if freeze {
                tasks.append(
                    Task {
                        // Cancellation means the loopback already ended; nothing to pause.
                        try? await Task.sleep(for: Tunables.Developer.freezeDelay)
                        guard !Task.isCancelled else { return }
                        try? hostCore.pauseStream(0)  // fails only if the session already ended
                    })
            }
        }

        private func loopbackMonitor() -> MonitorDescription {
            let size = Tunables.Developer.loopbackMonitor
            let millimeters = Tunables.Developer.testPhysicalSizeMillimeters
            return MonitorDescription(
                id: 0, widthPixels: size.width, heightPixels: size.height, refreshMillihertz: size.refreshMillihertz,
                physicalWidthMillimeters: Int(millimeters.width), physicalHeightMillimeters: Int(millimeters.height),
                scalePercent: 100, name: String(localized: "developer.displayName \(1)"))
        }

        private func registerHotkey() {
            do throws(HotkeyError) {
                hotkey = try dependencies.hotkeys.register(Tunables.Viewer.toggleHotkey) { [weak self] in
                    self?.toggleViewer()
                }
            } catch {
                errorMessage = String(localized: "error.hotkey.unavailable")
            }
        }

        private func refresh(_ statistics: ViewerStreamStatistics?) {
            guard let statistics else { return }
            let now = ContinuousClock.now
            if let last = lastSample, statistics.framesPresented >= lastPresented {
                let elapsed = (now - last).components
                let seconds = Double(elapsed.seconds) + Double(elapsed.attoseconds) / 1e18
                if seconds > 0 {
                    framesPerSecond = Double(statistics.framesPresented - lastPresented) / seconds
                }
            }
            lastPresented = statistics.framesPresented
            lastSample = now
            decodeErrors = statistics.decodeErrors
            lastRecoveryMilliseconds = statistics.lastRecoveryMilliseconds
        }

        private func hostEnded(_ failure: HostSessionError?) {
            if let failure { show(failure) }
            Task { [viewer] in await viewer?.stop() }
            finishIfDone()
        }

        private func viewerEnded(_ failure: CoreError?) {
            if let failure, errorMessage == nil { show(HostSessionError.session(failure)) }
            Task { [host] in await host?.stop() }
            finishIfDone()
        }

        /// Both sessions report their end; cleanup happens after the second.
        private var endedCount = 0
        private func finishIfDone() {
            endedCount += 1
            guard endedCount >= 2 else { return }
            endedCount = 0
            patterns.close()
            if let hotkey { dependencies.hotkeys.unregister(hotkey) }
            hotkey = nil
            tasks.forEach { $0.cancel() }
            tasks = []
            host = nil
            hostCore = nil
            viewer = nil
            isRunning = false
        }

        private func show(_ error: HostSessionError) {
            errorMessage = error.message
            needsScreenRecordingPermission = error.needsScreenRecordingPermission
        }

        private static func builtInDisplayID() -> CGDirectDisplayID {
            NSScreen.screens.compactMap(\.displayID).first { CGDisplayIsBuiltin($0) != 0 }
                ?? NSScreen.main?.displayID ?? CGMainDisplayID()
        }
    }
#endif
