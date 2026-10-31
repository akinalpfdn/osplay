import CoreBridge
import Foundation
import OSLog
import Observation
import SwiftUI

/// The iPad app's state: the computers nearby, the pairing prompt, and the one viewer session.
/// The iPad is a display only (DEVPLAN Phase 15): it never hosts and forwards no input.
@MainActor
@Observable
final class PadConnectionViewModel {
    enum Status: Equatable {
        case idle
        /// Listening on the USB cable until a sharing computer there connects (TAB-03).
        case waitingForCable
        case connecting(peer: String)
        case viewing(peer: String)
        /// The connection dropped (sleep, network, rotation); waiting for the computer again.
        case reconnecting(peer: String)
    }

    /// What a computer in the list can do with this iPad.
    enum DeviceState: Equatable {
        case paired
        case new
        /// Osplay 0.1 on the computer refuses iPads.
        case needsUpdate
        /// A different major protocol version.
        case incompatible
    }

    struct PairingRequest: Identifiable {
        let id = UUID()
        let code: UInt32
        let peerName: String
    }

    private(set) var devices: [DiscoveredDevice] = []
    private(set) var status: Status = .idle
    private(set) var failure: PadFailure?
    private(set) var pairing: PairingRequest?
    private(set) var peers: [TrustedPeer] = []
    /// The stream's view while viewing.
    private(set) var stage: PadPresenterView?

    var isViewerShown: Bool {
        switch status {
        case .connecting, .viewing: true
        case .idle, .waitingForCable, .reconnecting: false
        }
    }

    let settings: PadSettings
    private let dependencies: PadDependencies
    private let trust: PeerTrustCache
    private var identity: DeviceIdentity?
    private var browseTask: Task<Void, Never>?
    private var viewer: ViewerSession?
    private var viewerTasks: [Task<Void, Never>] = []
    private var viewerPeer = SessionPeer()
    private var pairingAnswer: ((Bool) -> Void)?
    /// The computer being viewed, for reconnecting.
    private var viewedHost: (deviceID: String, name: String)?
    private var offeredMonitor: MonitorDescription?
    private var endedByUser = false
    /// Set when the iPad itself closed the session to come back (rotation, foreground).
    private var resumesAfterClose = false
    /// Connect when this computer appears; nil when not waiting.
    private var awaitedDeviceID: String?
    private var reconnectTask: Task<Void, Never>?
    private var isInBackground = false
    /// The computer to connect to again when the app returns to the foreground.
    private var resumeDeviceID: String?
    /// The session runs over the USB cable.
    private var viaCable = false
    /// Wait on the cable again when the app returns to the foreground.
    private var resumesCable = false

    init(dependencies: PadDependencies) {
        self.dependencies = dependencies
        settings = dependencies.settings
        trust = PeerTrustCache(store: dependencies.keyStore)
        dependencies.presenters.onStageChange = { [weak self] view in self?.stage = view }
    }

    // MARK: - Nearby computers

    /// Loads the identity and paired computers and starts looking for computers.
    func start() {
        guard prepare() != nil, browseTask == nil else { return }
        let events = dependencies.discovery.browse()
        browseTask = Task { [weak self] in
            for await event in events {
                self?.discovered(event)
            }
            self?.browseTask = nil
        }
    }

    func state(of device: DiscoveredDevice) -> DeviceState {
        let advertisement = device.advertisement
        if advertisement.protocolVersion != Advertisement.protocolVersion { return .incompatible }
        if advertisement.protocolMinor == 0 { return .needsUpdate }
        return isPaired(advertisement.deviceID) ? .paired : .new
    }

    /// The paired computer's name, or the one it advertises.
    func displayName(of device: DiscoveredDevice) -> String {
        peers.first { DeviceIdentity.deviceID(publicKey: $0.publicKey) == device.advertisement.deviceID }?.name
            ?? device.advertisement.name
    }

    private func isPaired(_ deviceID: String) -> Bool {
        peers.contains { !$0.revoked && DeviceIdentity.deviceID(publicKey: $0.publicKey) == deviceID }
    }

    private func discovered(_ event: DiscoveryEvent) {
        switch event {
        case .found(let device):
            // Tablets and computers that only view cannot be extended onto.
            guard device.advertisement.roles.contains(.host) else { return }
            devices.removeAll { $0.label == device.label }
            devices.append(device)
            devices.sort { displayName(of: $0).localizedStandardCompare(displayName(of: $1)) == .orderedAscending }
            if device.advertisement.deviceID == awaitedDeviceID {
                awaitedDeviceID = nil
                connect(to: device)
            }
        case .lost(let label):
            devices.removeAll { $0.label == label }
        case .failed(let reason):
            failure = .discovery(reason)
            devices = []
        }
    }

    // MARK: - Viewing

    func connect(to device: DiscoveredDevice) {
        guard viewer == nil, let identity = prepare() else { return }
        switch state(of: device) {
        case .incompatible:
            failure = .incompatible
            return
        case .needsUpdate:
            failure = .needsUpdate(displayName(of: device))
            return
        case .paired, .new:
            break
        }
        guard let monitor = dependencies.screen.monitor() else {
            failure = .noScreen
            return
        }
        failure = nil
        reconnectTask?.cancel()
        awaitedDeviceID = nil
        let peer = SessionPeer()
        let core: CoreSession
        do throws(CoreError) {
            core = try CoreSession.viewer(
                device: sessionDevice(identity), monitors: [monitor], host: device.host, port: device.port,
                trust: { [trust, peer] key in peer.trust(key, in: trust) })
        } catch {
            failure = .session(error)
            return
        }
        let name = displayName(of: device)
        viewedHost = (device.advertisement.deviceID, name)
        status = .connecting(peer: name)
        run(core, peer: peer, monitor: monitor, viaCable: false)
    }

    /// Listens on the USB cable; a computer sharing its screen there connects within seconds. The
    /// computer opens the connection over the cable, so the iPad cannot pick one (TAB-03).
    func waitForCable() {
        guard viewer == nil, let identity = prepare() else { return }
        guard let monitor = dependencies.screen.monitor() else {
            failure = .noScreen
            return
        }
        failure = nil
        reconnectTask?.cancel()
        awaitedDeviceID = nil
        let peer = SessionPeer()
        let core: CoreSession
        do throws(CoreError) {
            core = try CoreSession.viewerOverUSB(
                device: sessionDevice(identity), monitors: [monitor],
                trust: { [trust, peer] key in peer.trust(key, in: trust) })
        } catch {
            failure = .session(error)
            return
        }
        viewedHost = nil
        status = .waitingForCable
        run(core, peer: peer, monitor: monitor, viaCable: true)
    }

    private func run(_ core: CoreSession, peer: SessionPeer, monitor: MonitorDescription, viaCable: Bool) {
        let session = ViewerSession(
            core: core, decoders: dependencies.decoders, presenters: dependencies.presenters,
            power: dependencies.power, target: { _ in .screen },
            showsStats: { [settings] in await MainActor.run { settings.showsStatsOverlay } })
        viewer = session
        viewerPeer = peer
        offeredMonitor = monitor
        endedByUser = false
        self.viaCable = viaCable
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

    /// Ends the session; nothing reconnects.
    func disconnect() {
        endedByUser = true
        awaitedDeviceID = nil
        resumeDeviceID = nil
        resumesCable = false
        reconnectTask?.cancel()
        if case .reconnecting = status { status = .idle }
        Task { [viewer] in await viewer?.stop() }
    }

    func answerPairing(_ accepted: Bool) {
        let answer = pairingAnswer
        pairing = nil
        pairingAnswer = nil
        answer?(accepted)
    }

    func dismissFailure() {
        failure = nil
    }

    /// The iPad turned: the host's display must take the new shape, so the session starts again.
    func viewportChanged(landscape: Bool) {
        guard let offered = offeredMonitor, viewer != nil, !endedByUser else { return }
        guard (offered.widthPixels > offered.heightPixels) != landscape else { return }
        resumesAfterClose = true
        Task { [viewer] in await viewer?.stop() }
    }

    /// iPadOS suspends a background app, so the session ends cleanly there (the host removes its
    /// display) and the same computer is connected again on return.
    func scenePhaseChanged(_ phase: ScenePhase) {
        switch phase {
        case .background:
            isInBackground = true
            guard viewer != nil, !endedByUser else { return }
            resumesAfterClose = true
            Task { [viewer] in await viewer?.stopForSleep() }
        case .active:
            isInBackground = false
            start()
            if resumesCable {
                resumesCable = false
                waitForCable()
            }
            if let deviceID = resumeDeviceID {
                resumeDeviceID = nil
                awaitHost(deviceID, after: .zero)
            }
        case .inactive:
            break
        @unknown default:
            break
        }
    }

    private func handle(_ event: ViewerSession.Event, from session: ViewerSession) {
        switch event {
        case .peer(let peer):
            if viaCable {
                // Over the cable the computer is known from here on; its ID follows with its key.
                viewedHost = ("", peer.deviceName)
                status = .connecting(peer: peer.deviceName)
            } else if let host = viewedHost, !isPaired(host.deviceID) {
                viewedHost = (host.deviceID, peer.deviceName)
            }
        case .pairingCode(let code, _):
            pairing = PairingRequest(code: code, peerName: viewedHost?.name ?? "")
            pairingAnswer = { accepted in Task { await session.confirmPairing(accepted) } }
        case .paired(let key):
            update { () throws(KeyStoreError) in try trust.paired(key, name: viewedHost?.name ?? "") }
        case .state(let state):
            if state == .streaming { viewerConnected() }
        case .roleSwapRequested:
            // An iPad cannot extend onto the computer.
            Task { await session.answerRoleSwap(.cannotHost) }
        case .streamAdded, .visibility, .statistics, .roleSwapAnswered:
            break
        }
    }

    private func viewerConnected() {
        if viaCable, let key = viewerPeer.key,
            let paired = peers.first(where: { $0.publicKey == key && !$0.revoked })
        {
            viewedHost = (DeviceIdentity.deviceID(publicKey: key), paired.name)
        }
        status = .viewing(peer: viewedHost?.name ?? "")
        if let key = viewerPeer.key {
            update { () throws(KeyStoreError) in try trust.seen(key) }
        }
    }

    private func viewerEnded(_ failure: CoreError?) {
        viewerTasks.forEach { $0.cancel() }
        viewerTasks = []
        viewer = nil
        stage = nil
        pairing = nil
        pairingAnswer = nil
        offeredMonitor = nil
        let host = viewedHost
        let resumes = resumesAfterClose
        resumesAfterClose = false
        let expectedBack = resumes || failure == .peerSleeping || failure == .timedOut || failure == .network
        if viaCable {
            viaCable = false
            viewedHost = nil
            status = .idle
            // The cable is waited on again only when the iPad closed the session itself or the
            // computer went away for a while; a user's disconnect on either side ends it.
            if expectedBack, !endedByUser {
                if isInBackground {
                    resumesCable = true
                } else {
                    waitForCable()
                }
            } else if let failure, failure != .connectionClosed {
                self.failure = .session(failure)
            }
            return
        }
        if let host, expectedBack, !endedByUser {
            status = .reconnecting(peer: host.name)
            if isInBackground {
                resumeDeviceID = host.deviceID
            } else {
                awaitHost(host.deviceID, after: resumes ? .zero : Tunables.Pad.reconnectDelay)
            }
            return
        }
        status = .idle
        viewedHost = nil
        if let failure, failure != .connectionClosed { self.failure = .session(failure) }
    }

    /// Connects when the computer is listed again (at once if it still is).
    private func awaitHost(_ deviceID: String, after delay: Duration) {
        reconnectTask?.cancel()
        reconnectTask = Task { [weak self] in
            try? await Task.sleep(for: delay)  // cancelled only when the user disconnects
            guard !Task.isCancelled, let self, viewer == nil else { return }
            if let device = devices.first(where: { $0.advertisement.deviceID == deviceID }) {
                connect(to: device)
            } else {
                awaitedDeviceID = deviceID
            }
        }
    }

    // MARK: - Paired computers

    func revoke(_ peer: TrustedPeer) {
        update { () throws(KeyStoreError) in try trust.revoke(peer.publicKey) }
    }

    func remove(_ peer: TrustedPeer) {
        update { () throws(KeyStoreError) in try trust.remove(peer.publicKey) }
    }

    private func prepare() -> DeviceIdentity? {
        if let identity { return identity }
        do throws(KeyStoreError) {
            let loaded = try dependencies.keyStore.identity()
            try trust.load()
            identity = loaded
            peers = trust.peers
            return loaded
        } catch {
            failure = .keyStore(error)
            return nil
        }
    }

    private func sessionDevice(_ identity: DeviceIdentity) -> SessionDevice {
        SessionDevice(
            identity: identity, name: settings.protocolDeviceName,
            capabilities: SessionCapabilities(
                maximumStreams: Tunables.Pad.maximumStreams, maximumWidth: Tunables.Pad.maximumWidth,
                maximumHeight: Tunables.Pad.maximumHeight, maximumRefreshHz: Tunables.Pad.maximumRefreshHz))
    }

    private func update(_ change: () throws(KeyStoreError) -> Void) {
        do throws(KeyStoreError) {
            try change()
        } catch {
            failure = .keyStore(error)
        }
        peers = trust.peers
    }
}
