#pragma once

// Control channel messages (docs/PROTOCOL.md, "Control channel"). One struct per message type;
// field names and ranges follow the document.

#include "protocol/constants.hpp"
#include "protocol/error.hpp"
#include "util/expected.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace osplay::protocol {

enum class ControlType : std::uint8_t {
    Hello = 0x01,
    HandshakeMessage = 0x02,
    PairingConfirm = 0x03,
    Capabilities = 0x04,
    ViewerMonitors = 0x05,
    StreamConfig = 0x06,
    StreamStart = 0x07,
    StreamPause = 0x08,
    StreamResume = 0x09,
    StreamStop = 0x0A,
    KeyframeRequest = 0x0B,
    Nack = 0x0C,
    ReceiverReport = 0x0D,
    Ping = 0x0E,
    Pong = 0x0F,
    Heartbeat = 0x10,
    Error = 0x11,
    Bye = 0x12,
    MediaSetup = 0x13,
};

enum class Role : std::uint8_t { Host = 1, Viewer = 2 };
enum class Platform : std::uint8_t { MacOS = 1, Windows = 2 };
enum class PairingDecision : std::uint8_t { Accepted = 1, Rejected = 2 };
enum class Codec : std::uint8_t { H264 = 1 };
enum class H264Profile : std::uint8_t { Baseline = 66, Main = 77, High = 100 };
enum class CursorMode : std::uint8_t { InVideo = 0, Separate = 1 };
enum class KeyframeReason : std::uint8_t {
    DecoderError = 1,
    FrameLost = 2,
    StreamStart = 3,
    StreamResume = 4
};
enum class ByeReason : std::uint8_t {
    UserDisconnected = 1,
    ApplicationQuit = 2,
    Sleeping = 3,
    Error = 4
};

inline constexpr std::uint32_t kCodecH264AnnexB = 1U << 0U;
inline constexpr std::uint8_t kCapabilitySendsCursor = 1U << 0U;
inline constexpr std::uint8_t kCapabilityRendersCursor = 1U << 1U;

using SessionToken = std::array<std::uint8_t, kSessionTokenSize>;

struct Hello {
    static constexpr ControlType kType = ControlType::Hello;
    Role role = Role::Host;
    Platform platform = Platform::MacOS;
    std::uint8_t protocol_minor = kMinorVersion;
    std::string device_name;
    bool operator==(const Hello&) const = default;
};

struct HandshakeMessage {
    static constexpr ControlType kType = ControlType::HandshakeMessage;
    std::uint8_t step = 1;
    std::vector<std::uint8_t> payload;
    bool operator==(const HandshakeMessage&) const = default;
};

struct PairingConfirm {
    static constexpr ControlType kType = ControlType::PairingConfirm;
    PairingDecision decision = PairingDecision::Accepted;
    bool operator==(const PairingConfirm&) const = default;
};

struct Capabilities {
    static constexpr ControlType kType = ControlType::Capabilities;
    std::uint32_t codecs = kCodecH264AnnexB;
    std::uint8_t max_streams = 1;
    std::uint8_t flags = 0;
    std::uint16_t max_width = 0;
    std::uint16_t max_height = 0;
    std::uint16_t max_refresh_hz = 0;
    bool operator==(const Capabilities&) const = default;
};

struct Monitor {
    std::uint8_t monitor_id = 0;
    std::uint16_t width_px = 0;
    std::uint16_t height_px = 0;
    std::uint32_t refresh_millihertz = 0;
    std::uint16_t physical_width_mm = 0;
    std::uint16_t physical_height_mm = 0;
    std::uint16_t scale_percent = 100;
    std::string name;
    bool operator==(const Monitor&) const = default;
};

struct ViewerMonitors {
    static constexpr ControlType kType = ControlType::ViewerMonitors;
    std::vector<Monitor> monitors;
    bool operator==(const ViewerMonitors&) const = default;
};

struct StreamConfig {
    static constexpr ControlType kType = ControlType::StreamConfig;
    std::uint8_t stream_id = 0;
    std::uint8_t monitor_id = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t refresh_millihertz = 0;
    Codec codec = Codec::H264;
    H264Profile profile = H264Profile::High;
    CursorMode cursor_mode = CursorMode::InVideo;
    std::uint32_t bitrate_kbps = 0;
    bool operator==(const StreamConfig&) const = default;
};

struct StreamStart {
    static constexpr ControlType kType = ControlType::StreamStart;
    std::uint8_t stream_id = 0;
    bool operator==(const StreamStart&) const = default;
};

struct StreamPause {
    static constexpr ControlType kType = ControlType::StreamPause;
    std::uint8_t stream_id = 0;
    bool operator==(const StreamPause&) const = default;
};

struct StreamResume {
    static constexpr ControlType kType = ControlType::StreamResume;
    std::uint8_t stream_id = 0;
    bool operator==(const StreamResume&) const = default;
};

struct StreamStop {
    static constexpr ControlType kType = ControlType::StreamStop;
    std::uint8_t stream_id = 0;
    bool operator==(const StreamStop&) const = default;
};

struct KeyframeRequest {
    static constexpr ControlType kType = ControlType::KeyframeRequest;
    std::uint8_t stream_id = 0;
    KeyframeReason reason = KeyframeReason::FrameLost;
    bool operator==(const KeyframeRequest&) const = default;
};

struct NackRange {
    std::uint16_t first_fragment = 0;
    std::uint16_t count = 0;
    bool operator==(const NackRange&) const = default;
};

struct Nack {
    static constexpr ControlType kType = ControlType::Nack;
    std::uint8_t stream_id = 0;
    std::uint32_t frame_id = 0;
    std::vector<NackRange> ranges;
    bool operator==(const Nack&) const = default;
};

struct ReceiverReport {
    static constexpr ControlType kType = ControlType::ReceiverReport;
    std::uint8_t stream_id = 0;
    std::uint16_t loss_permille = 0;
    std::uint32_t frames_received = 0;
    std::uint32_t frames_dropped = 0;
    std::uint32_t decode_time_us = 0;
    std::uint32_t jitter_us = 0;
    bool operator==(const ReceiverReport&) const = default;
};

struct Ping {
    static constexpr ControlType kType = ControlType::Ping;
    std::uint32_t ping_id = 0;
    std::uint64_t send_time_us = 0;
    bool operator==(const Ping&) const = default;
};

struct Pong {
    static constexpr ControlType kType = ControlType::Pong;
    std::uint32_t ping_id = 0;
    std::uint64_t ping_send_time_us = 0;
    std::uint64_t receive_time_us = 0;
    std::uint64_t reply_time_us = 0;
    bool operator==(const Pong&) const = default;
};

struct Heartbeat {
    static constexpr ControlType kType = ControlType::Heartbeat;
    bool operator==(const Heartbeat&) const = default;
};

/// The Error message (type 0x11); named PeerError to keep it apart from protocol::Error.
struct PeerError {
    static constexpr ControlType kType = ControlType::Error;
    std::uint16_t code = 0;
    bool operator==(const PeerError&) const = default;
};

struct Bye {
    static constexpr ControlType kType = ControlType::Bye;
    ByeReason reason = ByeReason::UserDisconnected;
    bool operator==(const Bye&) const = default;
};

struct MediaSetup {
    static constexpr ControlType kType = ControlType::MediaSetup;
    std::uint16_t udp_port = 0;
    SessionToken session_token{};
    bool operator==(const MediaSetup&) const = default;
};

using ControlMessage =
    std::variant<Hello, HandshakeMessage, PairingConfirm, Capabilities, ViewerMonitors,
                 StreamConfig, StreamStart, StreamPause, StreamResume, StreamStop, KeyframeRequest,
                 Nack, ReceiverReport, Ping, Pong, Heartbeat, PeerError, Bye, MediaSetup>;

[[nodiscard]] ControlType type_of(const ControlMessage& message);

/// Decodes one control message (the frame payload, without the length prefix).
[[nodiscard]] Expected<ControlMessage, Error> decode_control(std::span<const std::uint8_t> bytes);

/// Encodes one control message; fails with the error a decoder would report for invalid values.
[[nodiscard]] Expected<std::vector<std::uint8_t>, Error>
encode_control(const ControlMessage& message);

} // namespace osplay::protocol
