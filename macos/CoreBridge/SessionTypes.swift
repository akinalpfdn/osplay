import Foundation
import OsplayCore

/// Lifecycle of a core session (docs/PROTOCOL.md "Session flow").
public enum SessionState: Sendable, Equatable {
    case connecting
    case handshaking
    case awaitingPairing
    case negotiating
    case streaming
    case paused
    case closed

    init(_ raw: osplay_session_state) {
        switch raw {
        case OSPLAY_SESSION_CONNECTING: self = .connecting
        case OSPLAY_SESSION_HANDSHAKING: self = .handshaking
        case OSPLAY_SESSION_AWAITING_PAIRING: self = .awaitingPairing
        case OSPLAY_SESSION_NEGOTIATING: self = .negotiating
        case OSPLAY_SESSION_STREAMING: self = .streaming
        case OSPLAY_SESSION_PAUSED: self = .paused
        default: self = .closed
        }
    }
}

/// What the local trust store says about a peer's static key.
public enum TrustDecision: Sendable {
    /// Known key: no pairing code.
    case trusted
    /// First contact: show the pairing code.
    case unknown
    /// Refuse the session.
    case blocked

    var raw: osplay_trust {
        switch self {
        case .trusted: OSPLAY_TRUST_TRUSTED
        case .unknown: OSPLAY_TRUST_UNKNOWN
        case .blocked: OSPLAY_TRUST_BLOCKED
        }
    }
}

public enum PeerPlatform: Sendable, Equatable {
    case macOS
    case windows

    init(_ raw: osplay_platform) {
        self = raw == OSPLAY_PLATFORM_WINDOWS ? .windows : .macOS
    }

    var raw: osplay_platform {
        switch self {
        case .macOS: OSPLAY_PLATFORM_MACOS
        case .windows: OSPLAY_PLATFORM_WINDOWS
        }
    }
}

public struct PeerInfo: Sendable, Equatable {
    public let platform: PeerPlatform
    public let protocolMinor: UInt8
    public let deviceName: String
}

/// A monitor the viewer lends to the host (ViewerMonitors).
public struct MonitorDescription: Sendable, Hashable {
    public let id: UInt8
    public let widthPixels: Int
    public let heightPixels: Int
    public let refreshMillihertz: UInt32
    public let physicalWidthMillimeters: Int
    public let physicalHeightMillimeters: Int
    public let scalePercent: Int
    public let name: String

    public init(
        id: UInt8, widthPixels: Int, heightPixels: Int, refreshMillihertz: UInt32,
        physicalWidthMillimeters: Int, physicalHeightMillimeters: Int, scalePercent: Int, name: String
    ) {
        self.id = id
        self.widthPixels = widthPixels
        self.heightPixels = heightPixels
        self.refreshMillihertz = refreshMillihertz
        self.physicalWidthMillimeters = physicalWidthMillimeters
        self.physicalHeightMillimeters = physicalHeightMillimeters
        self.scalePercent = scalePercent
        self.name = name
    }

    public var refreshRate: Double { Double(refreshMillihertz) / 1000 }

    init(_ raw: osplay_monitor) {
        self.init(
            id: raw.monitor_id, widthPixels: Int(raw.width_px), heightPixels: Int(raw.height_px),
            refreshMillihertz: raw.refresh_millihertz, physicalWidthMillimeters: Int(raw.physical_width_mm),
            physicalHeightMillimeters: Int(raw.physical_height_mm), scalePercent: Int(raw.scale_percent),
            name: raw.name.map { String(cString: $0) } ?? "")
    }
}

/// What a device can encode or decode (Capabilities).
public struct SessionCapabilities: Sendable, Equatable {
    public let maximumStreams: Int
    public let maximumWidth: Int
    public let maximumHeight: Int
    public let maximumRefreshHz: Int

    public init(maximumStreams: Int, maximumWidth: Int, maximumHeight: Int, maximumRefreshHz: Int) {
        self.maximumStreams = maximumStreams
        self.maximumWidth = maximumWidth
        self.maximumHeight = maximumHeight
        self.maximumRefreshHz = maximumRefreshHz
    }

    var raw: osplay_capabilities {
        osplay_capabilities(
            codecs: OSPLAY_CODEC_H264_ANNEX_B, max_streams: UInt8(clamping: maximumStreams), flags: 0,
            max_width: UInt16(clamping: maximumWidth), max_height: UInt16(clamping: maximumHeight),
            max_refresh_hz: UInt16(clamping: maximumRefreshHz))
    }
}

public enum H264Profile: Sendable, Hashable {
    case baseline
    case main
    case high

    init(_ raw: osplay_h264_profile) {
        switch raw {
        case OSPLAY_H264_BASELINE: self = .baseline
        case OSPLAY_H264_MAIN: self = .main
        default: self = .high
        }
    }

    var raw: osplay_h264_profile {
        switch self {
        case .baseline: OSPLAY_H264_BASELINE
        case .main: OSPLAY_H264_MAIN
        case .high: OSPLAY_H264_HIGH
        }
    }
}

public enum CursorMode: Sendable, Hashable {
    /// The cursor is composited into the video.
    case inVideo
    /// The cursor travels as separate cursor packets.
    case separate

    init(_ raw: osplay_cursor_mode) {
        self = raw == OSPLAY_CURSOR_SEPARATE ? .separate : .inVideo
    }

    var raw: osplay_cursor_mode {
        switch self {
        case .inVideo: OSPLAY_CURSOR_IN_VIDEO
        case .separate: OSPLAY_CURSOR_SEPARATE
        }
    }
}

/// One video stream (StreamConfig); H.264 Annex B in protocol v1.
public struct StreamConfiguration: Sendable, Hashable {
    public let streamID: UInt8
    public let monitorID: UInt8
    public let width: Int
    public let height: Int
    public let refreshMillihertz: UInt32
    public let profile: H264Profile
    public let cursorMode: CursorMode
    public let bitrateKbps: Int

    public init(
        streamID: UInt8, monitorID: UInt8, width: Int, height: Int, refreshMillihertz: UInt32,
        profile: H264Profile, cursorMode: CursorMode, bitrateKbps: Int
    ) {
        self.streamID = streamID
        self.monitorID = monitorID
        self.width = width
        self.height = height
        self.refreshMillihertz = refreshMillihertz
        self.profile = profile
        self.cursorMode = cursorMode
        self.bitrateKbps = bitrateKbps
    }

    public var refreshRate: Double { Double(refreshMillihertz) / 1000 }

    init(_ raw: osplay_stream_config) {
        self.init(
            streamID: raw.stream_id, monitorID: raw.monitor_id, width: Int(raw.width), height: Int(raw.height),
            refreshMillihertz: raw.refresh_millihertz, profile: H264Profile(raw.profile),
            cursorMode: CursorMode(raw.cursor_mode), bitrateKbps: Int(raw.bitrate_kbps))
    }

    var raw: osplay_stream_config {
        osplay_stream_config(
            stream_id: streamID, monitor_id: monitorID, width: UInt16(clamping: width),
            height: UInt16(clamping: height), refresh_millihertz: refreshMillihertz, profile: profile.raw,
            cursor_mode: cursorMode.raw, bitrate_kbps: UInt32(clamping: bitrateKbps))
    }
}

/// One encoded H.264 frame in Annex B format.
public struct AccessUnit: Sendable, Equatable {
    public let streamID: UInt8
    public let isKeyframe: Bool
    /// The unit starts with SPS and PPS; only keyframes may carry them.
    public let carriesParameterSets: Bool
    /// Set by the viewer; ignored when pushing.
    public let frameID: UInt32
    /// Host clock, microseconds.
    public let captureTimeMicroseconds: UInt64
    public let data: Data

    public init(
        streamID: UInt8, isKeyframe: Bool, carriesParameterSets: Bool, frameID: UInt32 = 0,
        captureTimeMicroseconds: UInt64, data: Data
    ) {
        self.streamID = streamID
        self.isKeyframe = isKeyframe
        self.carriesParameterSets = carriesParameterSets
        self.frameID = frameID
        self.captureTimeMicroseconds = captureTimeMicroseconds
        self.data = data
    }
}

public struct StreamStatistics: Sendable, Equatable {
    public let streamID: UInt8
    public let lossPermille: Int
    public let framesPerSecond: Int
    public let bitrateKbps: Int
    /// Viewer: capture to delivery; 0 if unknown.
    public let latencyMicroseconds: Int
    public let framesDropped: UInt64
    public let retransmittedFragments: UInt64
    public let keyframeRequests: UInt64

    init(_ raw: osplay_stream_stats) {
        streamID = raw.stream_id
        lossPermille = Int(raw.loss_permille)
        framesPerSecond = Int(raw.frames_per_second)
        bitrateKbps = Int(raw.bitrate_kbps)
        latencyMicroseconds = Int(raw.latency_us)
        framesDropped = raw.frames_dropped
        retransmittedFragments = raw.retransmitted_fragments
        keyframeRequests = raw.keyframe_requests
    }
}

public struct SessionStatistics: Sendable, Equatable {
    public let roundTripMicroseconds: Int
    /// Peer clock minus local clock.
    public let clockOffsetMicroseconds: Int64
    public let datagramsRejected: UInt64
    public let streams: [StreamStatistics]
}

public enum SessionLogLevel: Sendable {
    case debug
    case info
    case warning
    case error

    init(_ raw: osplay_log_level) {
        switch raw {
        case OSPLAY_LOG_DEBUG: self = .debug
        case OSPLAY_LOG_INFO: self = .info
        case OSPLAY_LOG_WARNING: self = .warning
        default: self = .error
        }
    }
}

/// Everything a session reports, in the order the core reported it.
public enum SessionEvent: Sendable {
    /// `reason` is set when the session closed because of an error.
    case state(SessionState, reason: CoreError?)
    case peer(PeerInfo)
    /// First pairing: show the six-digit code, then call `confirmPairing`.
    case pairingCode(UInt32, peerPublicKey: [UInt8])
    /// Both users accepted: store the key as trusted.
    case paired(peerPublicKey: [UInt8])
    /// Host: answer with `configureStreams`.
    case viewerMonitors([MonitorDescription])
    /// Viewer: a stream the host is about to send.
    case streamConfiguration(StreamConfiguration)
    /// Host: encode a keyframe for this stream next.
    case keyframeRequested(streamID: UInt8)
    /// Viewer: a complete frame, in order.
    case accessUnit(AccessUnit)
    case statistics(SessionStatistics)
    case log(SessionLogLevel, String)
}

/// This device as the session presents it to the peer.
public struct SessionDevice: Sendable {
    public let identity: DeviceIdentity
    public let name: String
    public let capabilities: SessionCapabilities

    public init(identity: DeviceIdentity, name: String, capabilities: SessionCapabilities) {
        self.identity = identity
        self.name = name
        self.capabilities = capabilities
    }
}
