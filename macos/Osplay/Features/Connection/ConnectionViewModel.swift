import AppKit
import CoreBridge
import Foundation
import Observation

/// Temporary connection controls (DEVPLAN Phase 10; Phase 11 builds the final UI on top): share
/// this Mac's screens or use it as a display, the hosts Bonjour finds, pairing, and the paired
/// devices.
@MainActor
@Observable
final class ConnectionViewModel {
    enum Mode: Hashable, Sendable {
        case off
        /// Hosting: advertised over Bonjour, a viewer connects.
        case sharing
        /// Viewing: browsing for hosts, the user picks one.
        case viewing
    }

    enum Status: Equatable, Sendable {
        case idle
        case waitingForViewer
        case connecting(peer: String)
        case connected(peer: String)
        /// The roles are being reversed with this computer.
        case swapping(peer: String)
    }

    /// After an accepted role swap: the role this Mac takes once the session has closed.
    private struct PendingSwap {
        let role: Mode
        let peerDeviceID: String
        let peerName: String
    }

    private(set) var mode: Mode = .off
    private(set) var status: Status = .idle
    /// Hosts to connect to, by name.
    private(set) var devices: [DiscoveredDevice] = []
    private(set) var peers: [TrustedPeer] = []
    private(set) var error: ConnectionError?
    private(set) var hotkeyUnavailable = false

    private let dependencies: AppDependencies
    private let trust: PeerTrustCache
    private let deviceName: String
    private var identity: DeviceIdentity?
    private var modeTask: Task<Void, Never>?
    private var host: HostSession?
    private var viewer: ViewerSession?
    private var viewerTasks: [Task<Void, Never>] = []
    private var peerName = ""
    /// While extending: the displays standing in for the viewer's monitors, by monitor ID.
    private var liveDisplays: [UInt8: CGDirectDisplayID] = [:]
    private var liveDeviceID: String?
    private var hotkey: HotkeyRegistration?
    private var swapHotkey: HotkeyRegistration?
    private(set) var swapHotkeyUnavailable = false
    private var pendingSwap: PendingSwap?
    /// Viewing after a swap: connects to this device as soon as Bonjour finds it.
    private var autoConnectDeviceID: String?
    private var autoConnectTimeout: Task<Void, Never>?

    init(dependencies: AppDependencies, deviceName: String = Host.current().localizedName ?? "Mac") {
        self.dependencies = dependencies
        self.deviceName = deviceName
        trust = PeerTrustCache(store: dependencies.keyStore)
    }

    var isSessionActive: Bool {
        switch status {
        case .connecting, .connected: true
        case .idle, .waitingForViewer, .swapping: false
        }
    }

    func setMode(_ newMode: Mode) {
        guard newMode != mode else { return }
        stopAll()
        mode = newMode
        error = nil
        switch newMode {
        case .off:
            status = .idle
        case .sharing:
            startSharing()
        case .viewing:
            startBrowsing()
        }
    }

    func connect(to device: DiscoveredDevice) {
        guard viewer == nil, let identity = prepare() else { return }
        guard device.advertisement.protocolVersion == Advertisement.protocolVersion else {
            error = .incompatible
            return
        }
        guard let local = LocalMonitor.offered() else {
            error = .noDisplay
            return
        }
        error = nil
        let core: CoreSession
        do throws(CoreError) {
            core = try CoreSession.viewer(
                device: sessionDevice(identity, maximumStreams: 1), monitors: [local.description], host: device.host,
                port: device.port, trust: { [trust] key in trust.trust(key) })
        } catch {
            self.error = .session(error)
            return
        }
        let displayID = local.displayID
        let session = ViewerSession(
            core: core, decoders: dependencies.decoders, presenters: dependencies.presenters,
            power: dependencies.power, target: { _ in .fullScreen(displayID) },
            showsStats: { [settings = dependencies.settings] in await MainActor.run { settings.showsStatsOverlay } })
        viewer = session
        peerName = device.advertisement.name
        status = .connecting(peer: peerName)
        registerHotkey(for: session)
        viewerTasks = [
            Task { [weak self] in
                let failure = await session.run()
                self?.viewerEnded(failure)
            },
            Task { [weak self] in
                for await event in session.events {
                    self?.handle(event, from: session)
                }
            },
        ]
    }

    /// Ends the current session; sharing goes back to waiting for a viewer.
    func disconnect() {
        Task { [host, viewer] in
            await host?.stop()
            await viewer?.stop()
        }
    }

    /// Reverses the roles with the connected computer: this Mac hosts if it was the display, and
    /// becomes the display if it was hosting. The other computer must agree (it answers at once).
    func swapRoles() {
        guard case .connected = status, pendingSwap == nil else { return }
        Task { [weak self, host, viewer] in
            do throws(CoreError) {
                if let host {
                    try await host.requestRoleSwap()
                } else if let viewer {
                    try await viewer.requestRoleSwap()
                }
            } catch {
                self?.error = error == .unsupportedVersion ? .roleSwapUnsupported : .session(error)
            }
        }
    }

    /// Shown in place of the name the device introduced itself with.
    func rename(_ peer: TrustedPeer, to name: String) {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        update { () throws(KeyStoreError) in try trust.rename(peer.publicKey, to: trimmed) }
    }

    /// Whether a discovered device is already paired (no code when connecting).
    func isPaired(_ device: DiscoveredDevice) -> Bool {
        peers.contains {
            !$0.revoked && DeviceIdentity.deviceID(publicKey: $0.publicKey) == device.advertisement.deviceID
        }
    }

    /// The paired device's name, or the one it advertises.
    func displayName(of device: DiscoveredDevice) -> String {
        peers.first { DeviceIdentity.deviceID(publicKey: $0.publicKey) == device.advertisement.deviceID }?.name
            ?? device.advertisement.name
    }

    /// Loads the paired devices for Settings before any connection did.
    func loadPeers() {
        _ = prepare()
    }

    /// Refused from now on; a session with it ends at once.
    func revoke(_ peer: TrustedPeer) {
        update { () throws(KeyStoreError) in try trust.revoke(peer.publicKey) }
        endSession(with: peer.publicKey)
    }

    /// Forgotten: its next connection pairs again. A session with it ends at once.
    func remove(_ peer: TrustedPeer) {
        update { () throws(KeyStoreError) in try trust.remove(peer.publicKey) }
        endSession(with: peer.publicKey)
    }

    private func endSession(with publicKey: [UInt8]) {
        guard isSessionActive, trust.currentPeer == publicKey else { return }
        disconnect()
    }

    func openScreenRecordingSettings() {
        guard let url = dependencies.permissions.screenRecordingSettingsURL else { return }
        NSWorkspace.shared.open(url)
    }

    /// Read when the menu window opens; sharing needs it.
    var hasScreenRecordingAccess: Bool { dependencies.permissions.hasScreenRecordingAccess }

    func openLocalNetworkSettings() {
        guard let url = dependencies.permissions.localNetworkSettingsURL else { return }
        NSWorkspace.shared.open(url)
    }

    // MARK: - Sharing

    private func startSharing() {
        guard let identity = prepare() else {
            mode = .off
            return
        }
        status = .waitingForViewer
        modeTask = Task { [weak self] in
            await self?.shareLoop(identity)
        }
    }

    /// A core host session accepts one viewer, so a new one listens after each session.
    private func shareLoop(_ identity: DeviceIdentity) async {
        while mode == .sharing, !Task.isCancelled {
            let failure = await shareOnce(identity)
            if pendingSwap != nil {
                // Outside this task: switching modes cancels it.
                Task { [weak self] in self?.performSwap() }
                return
            }
            if let failure {
                error = failure
                if case .host(.screenRecordingPermissionMissing) = failure {
                    mode = .off
                    status = .idle
                    return
                }
                if case .discovery = failure {
                    mode = .off
                    status = .idle
                    return
                }
            }
            // Cancellation ends the loop through the condition above.
            try? await Task.sleep(for: Tunables.Connection.relistenDelay)
        }
    }

    private func shareOnce(_ identity: DeviceIdentity) async -> ConnectionError? {
        let core: CoreSession
        do throws(CoreError) {
            core = try CoreSession.host(
                device: sessionDevice(identity, maximumStreams: Tunables.VirtualDisplay.maximumDisplayCount),
                trust: { [trust] key in trust.trust(key) })
        } catch {
            return .session(error)
        }
        let advertising: any Advertising
        do throws(DiscoveryError) {
            advertising = try dependencies.discovery.advertise(
                Advertisement(identity: identity, name: deviceName, roles: .host), port: core.controlPort)
        } catch {
            core.close()
            return .discovery(error)
        }
        defer { advertising.cancel() }
        let settings = dependencies.settings
        let session = HostSession(
            core: core,
            policy: { [trust] in
                let key = trust.currentPeer ?? []
                return await MainActor.run {
                    let deviceID = DeviceIdentity.deviceID(publicKey: key)
                    return HostDisplayPolicy(
                        peerID: Data(key), hiDPI: settings.hiDPIChoice(for: deviceID),
                        arrangement: settings.arrangement(for: deviceID), quality: settings.qualityPreset)
                }
            },
            displays: VirtualDisplayLender(provider: dependencies.virtualDisplays), captures: dependencies.captures,
            encoders: dependencies.encoders, permissions: dependencies.permissions)
        host = session
        status = .waitingForViewer
        let events = Task { [weak self] in
            for await event in session.events {
                self?.handle(event, from: session)
            }
        }
        let failure = await session.run()
        events.cancel()
        unregisterSwapHotkey()
        host = nil
        liveDisplays = [:]
        liveDeviceID = nil
        dependencies.pairingPrompt.dismiss()
        if mode == .sharing { status = .waitingForViewer }
        return failure.map(ConnectionError.host)
    }

    private func handle(_ event: HostSession.Event, from session: HostSession) {
        switch event {
        case .peer(let peer):
            peerName = peer.deviceName
            status = .connecting(peer: peer.deviceName)
        case .pairingCode(let code, _):
            dependencies.pairingPrompt.show(code: code, peerName: peerName) { accepted in
                Task { await session.confirmPairing(accepted) }
            }
        case .paired(let key):
            storePairing(key)
        case .state(let state):
            if state == .streaming { connected() }
        case .streaming(let displays):
            rememberArrangement(displays)
        case .statistics:
            break
        case .roleSwapRequested(let key):
            answerSwap(from: key, becoming: .viewing) { answer in Task { await session.answerRoleSwap(answer) } }
        case .roleSwapAnswered(let answer, let key):
            swapAnswered(answer, peer: key, becoming: .viewing)
        }
    }

    // MARK: - Viewing

    private func startBrowsing() {
        guard prepare() != nil else {
            mode = .off
            return
        }
        status = .idle
        let events = dependencies.discovery.browse()
        modeTask = Task { [weak self] in
            for await event in events {
                self?.discovered(event)
            }
        }
    }

    private func discovered(_ event: DiscoveryEvent) {
        switch event {
        case .found(let device):
            guard device.advertisement.roles.contains(.host), device.advertisement.deviceID != identity?.deviceID
            else { return }
            devices.removeAll { $0.label == device.label }
            devices.append(device)
            devices.sort { $0.advertisement.name.localizedStandardCompare($1.advertisement.name) == .orderedAscending }
            if device.advertisement.deviceID == autoConnectDeviceID {
                autoConnectDeviceID = nil
                autoConnectTimeout?.cancel()
                connect(to: device)
            }
        case .lost(let label):
            devices.removeAll { $0.label == label }
        case .failed(let reason):
            error = .discovery(reason)
            devices = []
        }
    }

    private func handle(_ event: ViewerSession.Event, from session: ViewerSession) {
        switch event {
        case .peer(let peer):
            peerName = peer.deviceName
        case .pairingCode(let code, _):
            dependencies.pairingPrompt.show(code: code, peerName: peerName) { accepted in
                Task { await session.confirmPairing(accepted) }
            }
        case .paired(let key):
            storePairing(key)
        case .state(let state):
            if state == .streaming { connected() }
        case .streamAdded, .visibility, .statistics:
            break
        case .roleSwapRequested(let key):
            answerSwap(from: key, becoming: .sharing) { answer in Task { await session.answerRoleSwap(answer) } }
        case .roleSwapAnswered(let answer, let key):
            swapAnswered(answer, peer: key, becoming: .sharing)
        }
    }

    private func viewerEnded(_ failure: CoreError?) {
        if let failure { error = .session(failure) }
        if let hotkey { dependencies.hotkeys.unregister(hotkey) }
        hotkey = nil
        hotkeyUnavailable = false
        unregisterSwapHotkey()
        viewerTasks.forEach { $0.cancel() }
        viewerTasks = []
        viewer = nil
        dependencies.pairingPrompt.dismiss()
        status = .idle
        if pendingSwap != nil { performSwap() }
    }

    // MARK: - Role swap

    /// The other computer asks to reverse the roles: agree when this Mac can take its new role.
    private func answerSwap(
        from key: [UInt8], becoming role: Mode, answer: @escaping (RoleSwapAnswer) -> Void
    ) {
        let able = role == .sharing ? hasScreenRecordingAccess : LocalMonitor.offered() != nil
        guard able, pendingSwap == nil else {
            answer(able ? .busy : .cannotHost)
            return
        }
        pendingSwap = PendingSwap(
            role: role, peerDeviceID: DeviceIdentity.deviceID(publicKey: key), peerName: peerName)
        status = .swapping(peer: peerName)
        answer(.accepted)
    }

    private func swapAnswered(_ answer: RoleSwapAnswer, peer key: [UInt8], becoming role: Mode) {
        guard answer == .accepted else {
            error = .roleSwap(answer)
            return
        }
        pendingSwap = PendingSwap(
            role: role, peerDeviceID: DeviceIdentity.deviceID(publicKey: key), peerName: peerName)
        status = .swapping(peer: peerName)
    }

    /// The session closed after an agreed swap: take the new role. Viewing connects to the same
    /// computer as soon as it advertises itself as a host; it is already paired, so no code.
    private func performSwap() {
        guard let swap = pendingSwap else { return }
        pendingSwap = nil
        setMode(swap.role)
        guard swap.role == .viewing else { return }
        status = .swapping(peer: swap.peerName)
        if let device = devices.first(where: { $0.advertisement.deviceID == swap.peerDeviceID }) {
            connect(to: device)
            return
        }
        autoConnectDeviceID = swap.peerDeviceID
        autoConnectTimeout = Task { [weak self] in
            try? await Task.sleep(for: Tunables.Connection.swapReconnectTimeout)
            guard !Task.isCancelled, let self, autoConnectDeviceID == swap.peerDeviceID else { return }
            autoConnectDeviceID = nil
            error = .roleSwapPeerNotFound
            status = .idle
        }
    }

    private func registerSwapHotkey() {
        guard swapHotkey == nil else { return }
        do throws(HotkeyError) {
            swapHotkey = try dependencies.hotkeys.register(dependencies.settings.swapHotkey) { [weak self] in
                self?.swapRoles()
            }
            swapHotkeyUnavailable = false
        } catch {
            swapHotkeyUnavailable = true
        }
    }

    private func unregisterSwapHotkey() {
        if let swapHotkey { dependencies.hotkeys.unregister(swapHotkey) }
        swapHotkey = nil
        swapHotkeyUnavailable = false
    }

    private func registerHotkey(for session: ViewerSession) {
        do throws(HotkeyError) {
            hotkey = try dependencies.hotkeys.register(dependencies.settings.viewerHotkey) {
                Task { await session.toggleVisibility() }
            }
        } catch {
            hotkeyUnavailable = true
        }
    }

    // MARK: - Shared

    /// The identity and the paired devices, loaded once.
    private func prepare() -> DeviceIdentity? {
        if let identity { return identity }
        do throws(KeyStoreError) {
            let loaded = try dependencies.keyStore.identity()
            try trust.load()
            identity = loaded
            peers = trust.peers
            return loaded
        } catch {
            self.error = .keyStore(error)
            return nil
        }
    }

    private func sessionDevice(_ identity: DeviceIdentity, maximumStreams: Int) -> SessionDevice {
        SessionDevice(
            identity: identity, name: deviceName,
            capabilities: SessionCapabilities(
                maximumStreams: maximumStreams, maximumWidth: Tunables.Connection.maximumWidth,
                maximumHeight: Tunables.Connection.maximumHeight,
                maximumRefreshHz: Tunables.Connection.maximumRefreshHz))
    }

    /// Keeps where the viewer's displays sit, so Settings shows them and the next connection reuses it.
    private func rememberArrangement(_ displays: [LentDisplay]) {
        guard let key = trust.currentPeer else { return }
        let deviceID = DeviceIdentity.deviceID(publicKey: key)
        dependencies.settings.setArrangement(displays.compactMap(\.arrangement), for: deviceID)
        liveDisplays = Dictionary(uniqueKeysWithValues: displays.map { ($0.monitor.id, $0.displayID) })
        liveDeviceID = deviceID
    }

    /// Moves the displays of a running session at once; a saved arrangement alone waits for the
    /// next connection.
    func applyArrangement(_ arrangement: [ArrangedMonitor], for deviceID: String) {
        guard deviceID == liveDeviceID else { return }
        let origins = arrangement.compactMap { monitor in liveDisplays[monitor.id].map { ($0, monitor.origin) } }
        do throws(VirtualDisplayError) {
            try DisplayArranger.apply(origins)
        } catch {
            Log.virtualDisplay.error("Moving displays failed: \(String(describing: error), privacy: .public)")
        }
    }

    private func connected() {
        status = .connected(peer: peerName)
        registerSwapHotkey()
        if let key = trust.currentPeer {
            update { () throws(KeyStoreError) in try trust.seen(key) }
        }
    }

    private func storePairing(_ key: [UInt8]) {
        let name = peerName
        update { () throws(KeyStoreError) in try trust.paired(key, name: name) }
    }

    private func update(_ change: () throws(KeyStoreError) -> Void) {
        do throws(KeyStoreError) {
            try change()
        } catch {
            self.error = .keyStore(error)
        }
        peers = trust.peers
    }

    private func stopAll() {
        modeTask?.cancel()
        modeTask = nil
        autoConnectDeviceID = nil
        autoConnectTimeout?.cancel()
        autoConnectTimeout = nil
        disconnect()
        devices = []
        dependencies.pairingPrompt.dismiss()
    }
}
