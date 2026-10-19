#if DEBUG
    import AppKit
    import CoreBridge
    import Darwin
    import Foundation
    import Observation

    /// Debug-only host for a viewer on another machine (DEVPLAN Phase 9 acceptance, until Phase 10
    /// adds discovery): a real host session that waits on a TCP port and trusts any viewer, so the
    /// Windows `OsplayViewerTest --connect` can reach it by address.
    @MainActor
    @Observable
    final class DeveloperRemoteHostViewModel {
        /// Covers the displays with a moving pattern for frame-rate measurements; off, they show the
        /// desktop and stream only what changes.
        var showsPattern = false
        private(set) var isRunning = false
        /// The PowerShell command (run in the tool's folder) for each LAN address of this Mac.
        private(set) var commands: [String] = []
        private(set) var streamCount = 0
        private(set) var bitrateKbps = 0
        private(set) var errorMessage: String?
        private(set) var needsScreenRecordingPermission = false

        private let dependencies: AppDependencies
        private let patterns = TestPatternWindows()
        private var session: HostSession?
        private var tasks: [Task<Void, Never>] = []

        init(dependencies: AppDependencies) {
            self.dependencies = dependencies
        }

        func start() {
            guard !isRunning else { return }
            errorMessage = nil
            needsScreenRecordingPermission = false
            let permissions = dependencies.permissions
            guard permissions.hasScreenRecordingAccess || permissions.requestScreenRecordingAccess() else {
                show(.screenRecordingPermissionMissing)
                return
            }
            do throws(CoreError) {
                try startSession()
                isRunning = true
            } catch {
                show(.session(error))
            }
        }

        func stop() {
            Task { [session] in await session?.stop() }
        }

        func copy(_ command: String) {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(command, forType: .string)
        }

        func openScreenRecordingSettings() {
            guard let url = dependencies.permissions.screenRecordingSettingsURL else { return }
            NSWorkspace.shared.open(url)
        }

        private func startSession() throws(CoreError) {
            let capabilities = SessionCapabilities(
                maximumStreams: Tunables.VirtualDisplay.maximumDisplayCount, maximumWidth: 7680, maximumHeight: 4320,
                maximumRefreshHz: 240)
            let core = try CoreSession.host(
                device: SessionDevice(
                    identity: try .generate(), name: Tunables.Developer.remoteHostDeviceName,
                    capabilities: capabilities),
                trust: { _ in .trusted })
            let session = HostSession(
                core: core, peerID: Tunables.Developer.testPeerID,
                displays: VirtualDisplayLender(provider: dependencies.virtualDisplays),
                captures: dependencies.captures, encoders: dependencies.encoders,
                permissions: dependencies.permissions, dumpDirectory: nil)
            self.session = session
            commands = Self.lanAddresses().map { address in
                ".\\OsplayViewerTest.exe --connect \(address) \(core.controlPort) --seconds 300"
            }
            streamCount = 0
            bitrateKbps = 0
            tasks = [
                Task { [weak self] in
                    let failure = await session.run()
                    self?.finish(failure: failure)
                },
                Task { [weak self] in
                    for await event in session.events {
                        self?.handle(event)
                    }
                },
            ]
        }

        private func handle(_ event: HostSession.Event) {
            switch event {
            case .state:
                break
            case .streaming(let displays):
                streamCount = displays.count
                if showsPattern {
                    patterns.show(on: displays.map(\.displayID))
                }
            case .statistics(let statistics):
                bitrateKbps = statistics.streams.reduce(0) { $0 + $1.bitrateKbps }
            }
        }

        private func finish(failure: HostSessionError?) {
            patterns.close()
            session = nil
            tasks.forEach { $0.cancel() }
            tasks = []
            isRunning = false
            commands = []
            if let failure { show(failure) }
        }

        private func show(_ error: HostSessionError) {
            errorMessage = error.message
            needsScreenRecordingPermission = error.needsScreenRecordingPermission
        }

        /// IPv4 addresses of the active, non-loopback interfaces.
        private static func lanAddresses() -> [String] {
            var first: UnsafeMutablePointer<ifaddrs>?
            guard getifaddrs(&first) == 0, let first else { return [] }
            defer { freeifaddrs(first) }
            var addresses: [String] = []
            for interface in sequence(first: first, next: { $0.pointee.ifa_next }) {
                let flags = Int32(interface.pointee.ifa_flags)
                guard let address = interface.pointee.ifa_addr, address.pointee.sa_family == UInt8(AF_INET),
                    flags & IFF_UP != 0, flags & IFF_LOOPBACK == 0
                else { continue }
                var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
                if getnameinfo(
                    address, socklen_t(address.pointee.sa_len), &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST)
                    == 0
                {
                    let bytes = host.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }
                    addresses.append(String(decoding: bytes, as: UTF8.self))
                }
            }
            return addresses
        }
    }
#endif
