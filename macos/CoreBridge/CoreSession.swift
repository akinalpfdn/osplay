import Foundation
import OsplayCore

/// One host or viewer session of the shared core (docs/PROTOCOL.md "Session flow").
///
/// The core runs the session on its own I/O thread and reports through `events`. Commands may be
/// called from any thread; they wait (at most 5 s) until the I/O thread has executed them, so call
/// them from an actor rather than the main thread. `events` must be consumed promptly: it buffers
/// without limit, including access units.
public final class CoreSession: Sendable {
    public let events: AsyncStream<SessionEvent>

    private let handle: SessionHandle
    /// Holds the callbacks' context; must outlive the core session.
    private let box: CallbackBox

    /// Host: listens for one viewer on `controlPort`, or an ephemeral TCP port for 0 or when it is
    /// taken. Passing the previous session's port keeps a Bonjour advertisement right.
    public static func host(
        device: SessionDevice, controlPort: UInt16 = 0, trust: @escaping @Sendable ([UInt8]) -> TrustDecision
    ) throws(CoreError) -> CoreSession {
        try CoreSession(device: device, monitors: [], controlPort: controlPort, trust: trust) {
            config, secret, random, callbacks, out in
            osplay_host_session_create(config, secret, random, callbacks, out)
        }
    }

    /// Viewer: connects to `host` (name or address) on `port` and offers `monitors`.
    public static func viewer(
        device: SessionDevice, monitors: [MonitorDescription], host: String, port: UInt16,
        trust: @escaping @Sendable ([UInt8]) -> TrustDecision
    ) throws(CoreError) -> CoreSession {
        try CoreSession(device: device, monitors: monitors, trust: trust) { config, secret, random, callbacks, out in
            osplay_viewer_session_create(config, secret, random, callbacks, host, port, out)
        }
    }

    /// The loopback port the iPad's Osplay listens on for a computer on the USB cable.
    public static let usbPort = UInt16(OSPLAY_USB_PORT)

    /// Host over a USB cable: reaches the iPad `deviceID` (from `USBDeviceWatcher`) through usbmuxd.
    /// The session closes with `.network` when the iPad is not waiting for a computer.
    public static func hostOverUSB(
        device: SessionDevice, deviceID: UInt32, port: UInt16 = usbPort,
        trust: @escaping @Sendable ([UInt8]) -> TrustDecision
    ) throws(CoreError) -> CoreSession {
        try CoreSession(device: device, monitors: [], trust: trust) { config, secret, random, callbacks, out in
            osplay_host_session_create_usb(config, secret, random, callbacks, deviceID, port, out)
        }
    }

    /// Viewer on a USB cable: waits on the loopback `port` for a computer to connect through
    /// usbmuxd, without a time limit. Throws `.network` when the port is taken.
    public static func viewerOverUSB(
        device: SessionDevice, monitors: [MonitorDescription], port: UInt16 = usbPort,
        trust: @escaping @Sendable ([UInt8]) -> TrustDecision
    ) throws(CoreError) -> CoreSession {
        try CoreSession(device: device, monitors: monitors, trust: trust) { config, secret, random, callbacks, out in
            osplay_viewer_session_create_usb(config, secret, random, callbacks, port, out)
        }
    }

    private typealias Create = (
        UnsafePointer<osplay_session_config>, UnsafePointer<UInt8>, UnsafePointer<osplay_random_source>,
        UnsafePointer<osplay_session_callbacks>, UnsafeMutablePointer<OpaquePointer?>
    ) -> osplay_result

    private init(
        device: SessionDevice, monitors: [MonitorDescription], controlPort: UInt16 = 0,
        trust: @escaping @Sendable ([UInt8]) -> TrustDecision, create: Create
    ) throws(CoreError) {
        let (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
        let box = CallbackBox(continuation: continuation, trust: trust)
        var created: OpaquePointer?
        let result = withSessionConfig(device: device, monitors: monitors, controlPort: controlPort) { config in
            var random = SystemRandom.source
            var callbacks = box.callbacks
            return device.identity.secretKey.withUnsafeBufferPointer { secret in
                // DeviceIdentity guarantees OSPLAY_KEY_SIZE bytes.
                guard let secret = secret.baseAddress else { return OSPLAY_ERROR_INVALID_ARGUMENT }
                return create(config, secret, &random, &callbacks, &created)
            }
        }
        if let error = CoreError(result: result) {
            continuation.finish()
            throw error
        }
        guard let created else { throw .internalFailure }
        self.events = events
        self.box = box
        self.handle = SessionHandle(created)
    }

    deinit {
        handle.destroy()
        box.continuation.finish()
    }

    /// Host: the TCP port to advertise over Bonjour.
    public var controlPort: UInt16 {
        var port: UInt16 = 0
        _ = handle.withPointer { osplay_session_control_port($0, &port) }
        return port
    }

    /// Answers `.pairingCode` with the local user's decision.
    public func confirmPairing(_ accepted: Bool) throws(CoreError) {
        try check(handle.withPointer { osplay_session_confirm_pairing($0, accepted ? 1 : 0) })
    }

    /// Host: starts one stream per configuration, once, after `.viewerMonitors`.
    public func configureStreams(_ streams: [StreamConfiguration]) throws(CoreError) {
        let raw = streams.map(\.raw)
        try check(
            handle.withPointer { pointer in
                raw.withUnsafeBufferPointer { osplay_session_configure_streams(pointer, $0.baseAddress, $0.count) }
            })
    }

    /// Host: queues one encoded frame; the data is copied. Returns without waiting for the network.
    public func push(_ unit: AccessUnit) throws(CoreError) {
        try check(
            handle.withPointer { pointer in
                unit.data.withUnsafeBytes { bytes in
                    var raw = osplay_access_unit(
                        stream_id: unit.streamID, keyframe: unit.isKeyframe ? 1 : 0,
                        codec_config: unit.carriesParameterSets ? 1 : 0, frame_id: 0,
                        capture_time_us: unit.captureTimeMicroseconds,
                        data: bytes.bindMemory(to: UInt8.self).baseAddress, length: bytes.count)
                    return osplay_session_push_access_unit(pointer, &raw)
                }
            })
    }

    /// Host: a new cursor image; it is resent after every keyframe.
    public func push(_ shape: CursorShape) throws(CoreError) {
        try check(
            handle.withPointer { pointer in
                shape.bgra.withUnsafeBytes { bytes in
                    var raw = osplay_cursor_shape(
                        stream_id: shape.streamID, shape_id: shape.shapeID, width: UInt16(clamping: shape.width),
                        height: UInt16(clamping: shape.height), hotspot_x: UInt16(clamping: shape.hotspotX),
                        hotspot_y: UInt16(clamping: shape.hotspotY),
                        bgra: bytes.bindMemory(to: UInt8.self).baseAddress, length: bytes.count)
                    return osplay_session_push_cursor_shape(pointer, &raw)
                }
            })
    }

    /// Host: where the cursor is; at most once per refresh.
    public func push(_ position: CursorPosition) throws(CoreError) {
        var raw = osplay_cursor_position(
            stream_id: position.streamID, visible: position.isVisible ? 1 : 0, shape_id: position.shapeID,
            x: position.x, y: position.y, capture_time_us: position.captureTimeMicroseconds)
        try check(handle.withPointer { osplay_session_push_cursor_position($0, &raw) })
    }

    /// Viewer: the decoder failed; asks the host for a keyframe (rate-limited by the core).
    public func requestKeyframe(_ streamID: UInt8) throws(CoreError) {
        try check(handle.withPointer { osplay_session_request_keyframe($0, streamID) })
    }

    /// Ends one stream for good on both sides; the session stays up.
    public func stopStream(_ streamID: UInt8) throws(CoreError) {
        try check(handle.withPointer { osplay_session_stop_stream($0, streamID) })
    }

    public func pauseStream(_ streamID: UInt8) throws(CoreError) {
        try check(handle.withPointer { osplay_session_pause_stream($0, streamID) })
    }

    public func resumeStream(_ streamID: UInt8) throws(CoreError) {
        try check(handle.withPointer { osplay_session_resume_stream($0, streamID) })
    }

    /// Asks the peer to reverse the roles; the answer arrives as `.roleSwapAnswered`.
    public func requestRoleSwap() throws(CoreError) {
        try check(handle.withPointer { osplay_session_request_role_swap($0) })
    }

    /// Answers `.roleSwapRequested`: `.accepted` closes the session, a refusal keeps it running.
    public func answerRoleSwap(_ answer: RoleSwapAnswer) throws(CoreError) {
        try check(handle.withPointer { osplay_session_answer_role_swap($0, answer.raw) })
    }

    /// Tells the peer this Mac is going to sleep and closes; the session reports `.closed` later.
    public func closeForSleep() {
        _ = handle.withPointer { osplay_session_close_for_sleep($0) }
    }

    /// Host: turns the viewer away because no virtual display is left for it (the viewer's session
    /// closes with `.peerBusy`); this session reports `.closed` later.
    public func refuseBusy() {
        _ = handle.withPointer { osplay_session_refuse_busy($0) }
    }

    /// Sends Bye when possible and stops the I/O thread; the session reports `.closed` first.
    /// Later commands fail with `.invalidState`.
    public func close() {
        handle.destroy()
        box.continuation.finish()
    }

    private func check(_ result: osplay_result?) throws(CoreError) {
        guard let result else { throw .invalidState }
        if let error = CoreError(result: result) { throw error }
    }
}

/// The core handle, destroyed exactly once. A lock orders commands against destruction.
private final class SessionHandle: @unchecked Sendable {
    private let lock = NSLock()
    private var pointer: OpaquePointer?

    init(_ pointer: OpaquePointer) { self.pointer = pointer }

    /// `nil` once the session is destroyed.
    func withPointer(_ body: (OpaquePointer) -> osplay_result) -> osplay_result? {
        lock.lock()
        defer { lock.unlock() }
        return pointer.map(body)
    }

    func destroy() {
        lock.lock()
        let destroyed = pointer
        pointer = nil
        lock.unlock()
        // Outside the lock: destroying joins the I/O thread, whose callbacks never take this lock.
        osplay_session_destroy(destroyed)
    }
}

/// Context of the C callbacks: forwards everything to the event stream.
private final class CallbackBox: Sendable {
    let continuation: AsyncStream<SessionEvent>.Continuation
    let trust: @Sendable ([UInt8]) -> TrustDecision

    init(continuation: AsyncStream<SessionEvent>.Continuation, trust: @escaping @Sendable ([UInt8]) -> TrustDecision) {
        self.continuation = continuation
        self.trust = trust
    }

    var callbacks: osplay_session_callbacks {
        var callbacks = osplay_session_callbacks()
        callbacks.context = Unmanaged.passUnretained(self).toOpaque()
        callbacks.on_state = { context, state, reason in
            CallbackBox.from(context)?.continuation.yield(
                .state(SessionState(state), reason: CoreError(result: reason)))
        }
        callbacks.on_peer = { context, peer in
            guard let peer = peer?.pointee else { return }
            let info = PeerInfo(
                platform: PeerPlatform(peer.platform), protocolMinor: peer.protocol_minor,
                deviceName: peer.device_name.map { String(cString: $0) } ?? "")
            CallbackBox.from(context)?.continuation.yield(.peer(info))
        }
        callbacks.on_trust_query = { context, key in
            CallbackBox.from(context)?.trust(keyBytes(key)).raw ?? OSPLAY_TRUST_BLOCKED
        }
        callbacks.on_pairing_code = { context, code, key in
            CallbackBox.from(context)?.continuation.yield(.pairingCode(code, peerPublicKey: keyBytes(key)))
        }
        callbacks.on_paired = { context, key in
            CallbackBox.from(context)?.continuation.yield(.paired(peerPublicKey: keyBytes(key)))
        }
        callbacks.on_viewer_monitors = { context, monitors, count in
            let list = UnsafeBufferPointer(start: monitors, count: count).map(MonitorDescription.init)
            CallbackBox.from(context)?.continuation.yield(.viewerMonitors(list))
        }
        callbacks.on_stream_config = { context, config in
            guard let config = config?.pointee else { return }
            CallbackBox.from(context)?.continuation.yield(.streamConfiguration(StreamConfiguration(config)))
        }
        callbacks.on_keyframe_request = { context, streamID in
            CallbackBox.from(context)?.continuation.yield(.keyframeRequested(streamID: streamID))
        }
        callbacks.on_access_unit = { context, unit in
            guard let unit = unit?.pointee else { return }
            let data = unit.data.map { Data(bytes: $0, count: unit.length) } ?? Data()
            let accessUnit = AccessUnit(
                streamID: unit.stream_id, isKeyframe: unit.keyframe == 1, carriesParameterSets: unit.codec_config == 1,
                frameID: unit.frame_id, captureTimeMicroseconds: unit.capture_time_us, data: data)
            CallbackBox.from(context)?.continuation.yield(.accessUnit(accessUnit))
        }
        callbacks.on_stream_stopped = { context, streamID in
            CallbackBox.from(context)?.continuation.yield(.streamStopped(streamID: streamID))
        }
        callbacks.on_cursor_shape = { context, shape in
            guard let shape = shape?.pointee else { return }
            let image = shape.bgra.map { Data(bytes: $0, count: shape.length) } ?? Data()
            let cursor = CursorShape(
                streamID: shape.stream_id, shapeID: shape.shape_id, width: Int(shape.width),
                height: Int(shape.height), hotspotX: Int(shape.hotspot_x), hotspotY: Int(shape.hotspot_y),
                bgra: image)
            CallbackBox.from(context)?.continuation.yield(.cursorShape(cursor))
        }
        callbacks.on_cursor_position = { context, position in
            guard let position = position?.pointee else { return }
            let cursor = CursorPosition(
                streamID: position.stream_id, isVisible: position.visible == 1, shapeID: position.shape_id,
                x: position.x, y: position.y, captureTimeMicroseconds: position.capture_time_us)
            CallbackBox.from(context)?.continuation.yield(.cursorPosition(cursor))
        }
        callbacks.on_stats = { context, stats in
            guard let stats = stats?.pointee else { return }
            let statistics = SessionStatistics(
                roundTripMicroseconds: Int(stats.rtt_us), clockOffsetMicroseconds: stats.clock_offset_us,
                datagramsRejected: stats.datagrams_rejected,
                streams: UnsafeBufferPointer(start: stats.streams, count: stats.stream_count).map(StreamStatistics.init)
            )
            CallbackBox.from(context)?.continuation.yield(.statistics(statistics))
        }
        callbacks.on_role_swap_request = { context, key in
            CallbackBox.from(context)?.continuation.yield(.roleSwapRequested(peerPublicKey: keyBytes(key)))
        }
        callbacks.on_role_swap_answer = { context, answer, key in
            CallbackBox.from(context)?.continuation.yield(
                .roleSwapAnswered(RoleSwapAnswer(answer), peerPublicKey: keyBytes(key)))
        }
        callbacks.on_bitrate_target = { context, streamID, kbps in
            CallbackBox.from(context)?.continuation.yield(.bitrateTarget(streamID: streamID, kbps: Int(kbps)))
        }
        callbacks.on_log = { context, level, message in
            let text = message.map { String(cString: $0) } ?? ""
            CallbackBox.from(context)?.continuation.yield(.log(SessionLogLevel(level), text))
        }
        return callbacks
    }

    /// The core passes back the context it was given; CoreSession keeps the box alive.
    static func from(_ context: UnsafeMutableRawPointer?) -> CallbackBox? {
        context.map { Unmanaged<CallbackBox>.fromOpaque($0).takeUnretainedValue() }
    }
}

private func keyBytes(_ key: UnsafePointer<UInt8>?) -> [UInt8] {
    guard let key else { return [] }
    return Array(UnsafeBufferPointer(start: key, count: Int(OSPLAY_KEY_SIZE)))
}

/// Builds the C configuration; every string stays valid for the duration of `body`.
private func withSessionConfig(
    device: SessionDevice, monitors: [MonitorDescription], controlPort: UInt16,
    _ body: (UnsafePointer<osplay_session_config>) -> osplay_result
) -> osplay_result {
    let names: [UnsafeMutablePointer<CChar>?] = monitors.map { strdup($0.name) }
    defer { names.forEach { free($0) } }
    let rawMonitors = zip(monitors, names).map { monitor, name in
        osplay_monitor(
            monitor_id: monitor.id, width_px: UInt16(clamping: monitor.widthPixels),
            height_px: UInt16(clamping: monitor.heightPixels), refresh_millihertz: monitor.refreshMillihertz,
            physical_width_mm: UInt16(clamping: monitor.physicalWidthMillimeters),
            physical_height_mm: UInt16(clamping: monitor.physicalHeightMillimeters),
            scale_percent: UInt16(clamping: monitor.scalePercent), name: UnsafePointer(name))
    }
    return device.name.withCString { name in
        rawMonitors.withUnsafeBufferPointer { monitorBuffer in
            var config = osplay_session_config(
                platform: PeerPlatform.current.raw, device_name: name, capabilities: device.capabilities.raw,
                monitors: monitorBuffer.isEmpty ? nil : monitorBuffer.baseAddress, monitor_count: monitorBuffer.count,
                control_port: controlPort)
            return body(&config)
        }
    }
}
