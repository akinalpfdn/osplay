#include "protocol/byte_io.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/text.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace osplay::protocol {
namespace {

using Check = Status<Error>;
using Decoded = Expected<ControlMessage, Error>;

// The standard visitor idiom: one call operator per lambda.
template <typename... Visitors>
struct Overloaded : Visitors... { // NOLINT(misc-multiple-inheritance)
    using Visitors::operator()...;
};

Check ok() {
    return Check::ok();
}
Check fail(Error error) {
    return Check::failure(error);
}
Decoded decode_failure(Error error) {
    return Decoded::failure(error);
}

bool in_range(std::uint32_t value, std::uint32_t low, std::uint32_t high) {
    return value >= low && value <= high;
}
bool valid_stream(std::uint8_t stream_id) {
    return stream_id < kMaxStreams;
}

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

Check check_text(const std::string& text, bool allow_empty) {
    if ((!allow_empty && text.empty()) || text.size() > kMaxStringBytes ||
        !is_valid_text(bytes_of(text))) {
        return fail(Error::InvalidString);
    }
    return ok();
}

// --- Validation, shared by encoder and decoder ---------------------------------------------------

Check validate(const Hello& m) {
    const bool role_ok = m.role == Role::Host || m.role == Role::Viewer;
    const bool platform_ok = m.platform == Platform::MacOS || m.platform == Platform::Windows;
    if (!role_ok || !platform_ok) {
        return fail(Error::ValueOutOfRange);
    }
    return check_text(m.device_name, false);
}

Check validate(const HandshakeMessage& m) {
    if (!in_range(m.step, 1, 3) || m.payload.empty() || m.payload.size() > kMaxHandshakePayload) {
        return fail(Error::ValueOutOfRange);
    }
    return ok();
}

Check validate(const PairingConfirm& m) {
    const bool known =
        m.decision == PairingDecision::Accepted || m.decision == PairingDecision::Rejected;
    return known ? ok() : fail(Error::ValueOutOfRange);
}

Check validate(const Capabilities& m) {
    const bool codecs_ok = m.codecs != 0 && (m.codecs & ~kCodecH264AnnexB) == 0;
    const auto known_flags =
        static_cast<std::uint8_t>(kCapabilitySendsCursor | kCapabilityRendersCursor);
    const bool flags_ok = (m.flags & static_cast<std::uint8_t>(~known_flags)) == 0;
    if (!codecs_ok || !flags_ok || !in_range(m.max_streams, 1, kMaxStreams) ||
        !in_range(m.max_width, 320, 16384) || !in_range(m.max_height, 240, 16384) ||
        !in_range(m.max_refresh_hz, 24, 1000)) {
        return fail(Error::ValueOutOfRange);
    }
    return ok();
}

Check validate(const ViewerMonitors& m) {
    if (m.monitors.empty() || m.monitors.size() > kMaxMonitors) {
        return fail(Error::ValueOutOfRange);
    }
    std::set<std::uint8_t> ids;
    for (const Monitor& monitor : m.monitors) {
        if (!ids.insert(monitor.monitor_id).second || !in_range(monitor.width_px, 320, 16384) ||
            !in_range(monitor.height_px, 240, 16384) ||
            !in_range(monitor.refresh_millihertz, 1000, 500'000) ||
            monitor.physical_width_mm > 10'000 || monitor.physical_height_mm > 10'000 ||
            !in_range(monitor.scale_percent, 100, 400)) {
            return fail(Error::ValueOutOfRange);
        }
        if (const Check name = check_text(monitor.name, true); !name) {
            return name;
        }
    }
    return ok();
}

Check validate(const StreamConfig& m) {
    const bool profile_ok = m.profile == H264Profile::Baseline || m.profile == H264Profile::Main ||
                            m.profile == H264Profile::High;
    const bool cursor_ok =
        m.cursor_mode == CursorMode::InVideo || m.cursor_mode == CursorMode::Separate;
    if (!valid_stream(m.stream_id) || !in_range(m.width, 320, 8192) || m.width % 2 != 0 ||
        !in_range(m.height, 240, 8192) || m.height % 2 != 0 ||
        !in_range(m.refresh_millihertz, 1000, 240'000) || m.codec != Codec::H264 || !profile_ok ||
        !cursor_ok || !in_range(m.bitrate_kbps, 100, 1'000'000)) {
        return fail(Error::ValueOutOfRange);
    }
    return ok();
}

template <typename StreamMessage>
Check validate_stream_only(const StreamMessage& m) {
    return valid_stream(m.stream_id) ? ok() : fail(Error::ValueOutOfRange);
}
Check validate(const StreamStart& m) {
    return validate_stream_only(m);
}
Check validate(const StreamPause& m) {
    return validate_stream_only(m);
}
Check validate(const StreamResume& m) {
    return validate_stream_only(m);
}
Check validate(const StreamStop& m) {
    return validate_stream_only(m);
}

Check validate(const KeyframeRequest& m) {
    const auto reason = static_cast<std::uint8_t>(m.reason);
    return valid_stream(m.stream_id) && in_range(reason, 1, 4) ? ok()
                                                               : fail(Error::ValueOutOfRange);
}

Check validate(const Nack& m) {
    if (!valid_stream(m.stream_id) || m.ranges.empty() || m.ranges.size() > kMaxNackRanges) {
        return fail(Error::ValueOutOfRange);
    }
    const bool ranges_ok = std::ranges::all_of(m.ranges, [](const NackRange& range) {
        return range.count >= 1 &&
               std::uint32_t{range.first_fragment} + range.count <= kMaxFragmentsPerFrame;
    });
    return ranges_ok ? ok() : fail(Error::ValueOutOfRange);
}

Check validate(const ReceiverReport& m) {
    return valid_stream(m.stream_id) && m.loss_permille <= 1000 ? ok()
                                                                : fail(Error::ValueOutOfRange);
}

Check validate(const Ping& /*m*/) {
    return ok();
}
Check validate(const Pong& /*m*/) {
    return ok();
}
Check validate(const Heartbeat& /*m*/) {
    return ok();
}
Check validate(const PeerError& /*m*/) {
    return ok(); // codes from newer versions are accepted and shown generically
}

Check validate(const Bye& m) {
    return in_range(static_cast<std::uint8_t>(m.reason), 1, 4) ? ok()
                                                               : fail(Error::ValueOutOfRange);
}

Check validate(const MediaSetup& m) {
    return m.udp_port != 0 ? ok() : fail(Error::ValueOutOfRange);
}

// --- Decoding ------------------------------------------------------------------------------------

bool read_text(Reader& reader, std::string& out) {
    std::uint8_t length = 0;
    std::span<const std::uint8_t> raw;
    if (!reader.u8(length) || !reader.bytes(length, raw)) {
        return false;
    }
    out.assign(raw.begin(), raw.end());
    return true;
}

Decoded read_hello(Reader& r) {
    std::uint8_t role = 0;
    std::uint8_t platform = 0;
    std::uint8_t minor = 0;
    std::uint8_t reserved = 0;
    std::string name;
    if (!r.u8(role) || !r.u8(platform) || !r.u8(minor) || !r.u8(reserved) || !read_text(r, name)) {
        return decode_failure(Error::Truncated);
    }
    if (reserved != 0) {
        return decode_failure(Error::ValueOutOfRange);
    }
    return Hello{.role = static_cast<Role>(role),
                 .platform = static_cast<Platform>(platform),
                 .protocol_minor = minor,
                 .device_name = std::move(name)};
}

Decoded read_handshake(Reader& r) {
    std::uint8_t step = 0;
    std::uint8_t reserved = 0;
    std::uint16_t length = 0;
    std::span<const std::uint8_t> payload;
    if (!r.u8(step) || !r.u8(reserved) || !r.u16(length) || !r.bytes(length, payload)) {
        return decode_failure(Error::Truncated);
    }
    if (reserved != 0) {
        return decode_failure(Error::ValueOutOfRange);
    }
    return HandshakeMessage{.step = step, .payload = {payload.begin(), payload.end()}};
}

Decoded read_pairing_confirm(Reader& r) {
    std::uint8_t decision = 0;
    if (!r.u8(decision)) {
        return decode_failure(Error::Truncated);
    }
    return PairingConfirm{.decision = static_cast<PairingDecision>(decision)};
}

Decoded read_capabilities(Reader& r) {
    Capabilities m;
    if (!r.u32(m.codecs) || !r.u8(m.max_streams) || !r.u8(m.flags) || !r.u16(m.max_width) ||
        !r.u16(m.max_height) || !r.u16(m.max_refresh_hz)) {
        return decode_failure(Error::Truncated);
    }
    return m;
}

Decoded read_viewer_monitors(Reader& r) {
    std::uint8_t count = 0;
    if (!r.u8(count)) {
        return decode_failure(Error::Truncated);
    }
    if (count == 0 || count > kMaxMonitors) {
        return decode_failure(Error::ValueOutOfRange);
    }
    ViewerMonitors m;
    m.monitors.resize(count);
    for (Monitor& monitor : m.monitors) {
        if (!r.u8(monitor.monitor_id) || !r.u16(monitor.width_px) || !r.u16(monitor.height_px) ||
            !r.u32(monitor.refresh_millihertz) || !r.u16(monitor.physical_width_mm) ||
            !r.u16(monitor.physical_height_mm) || !r.u16(monitor.scale_percent) ||
            !read_text(r, monitor.name)) {
            return decode_failure(Error::Truncated);
        }
    }
    return m;
}

Decoded read_stream_config(Reader& r) {
    StreamConfig m;
    std::uint8_t codec = 0;
    std::uint8_t profile = 0;
    std::uint8_t cursor_mode = 0;
    std::uint8_t reserved = 0;
    if (!r.u8(m.stream_id) || !r.u8(m.monitor_id) || !r.u16(m.width) || !r.u16(m.height) ||
        !r.u32(m.refresh_millihertz) || !r.u8(codec) || !r.u8(profile) || !r.u8(cursor_mode) ||
        !r.u8(reserved) || !r.u32(m.bitrate_kbps)) {
        return decode_failure(Error::Truncated);
    }
    if (reserved != 0) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.codec = static_cast<Codec>(codec);
    m.profile = static_cast<H264Profile>(profile);
    m.cursor_mode = static_cast<CursorMode>(cursor_mode);
    return m;
}

template <typename StreamMessage>
Decoded read_stream_only(Reader& r) {
    StreamMessage m;
    if (!r.u8(m.stream_id)) {
        return decode_failure(Error::Truncated);
    }
    return m;
}

Decoded read_keyframe_request(Reader& r) {
    std::uint8_t stream_id = 0;
    std::uint8_t reason = 0;
    if (!r.u8(stream_id) || !r.u8(reason)) {
        return decode_failure(Error::Truncated);
    }
    return KeyframeRequest{.stream_id = stream_id, .reason = static_cast<KeyframeReason>(reason)};
}

Decoded read_nack(Reader& r) {
    Nack m;
    std::uint8_t count = 0;
    if (!r.u8(m.stream_id) || !r.u32(m.frame_id) || !r.u8(count)) {
        return decode_failure(Error::Truncated);
    }
    if (count == 0 || count > kMaxNackRanges) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.ranges.resize(count);
    for (NackRange& range : m.ranges) {
        if (!r.u16(range.first_fragment) || !r.u16(range.count)) {
            return decode_failure(Error::Truncated);
        }
    }
    return m;
}

Decoded read_receiver_report(Reader& r) {
    ReceiverReport m;
    std::uint8_t reserved = 0;
    if (!r.u8(m.stream_id) || !r.u8(reserved) || !r.u16(m.loss_permille) ||
        !r.u32(m.frames_received) || !r.u32(m.frames_dropped) || !r.u32(m.decode_time_us) ||
        !r.u32(m.jitter_us)) {
        return decode_failure(Error::Truncated);
    }
    if (reserved != 0) {
        return decode_failure(Error::ValueOutOfRange);
    }
    return m;
}

Decoded read_ping(Reader& r) {
    Ping m;
    if (!r.u32(m.ping_id) || !r.u64(m.send_time_us)) {
        return decode_failure(Error::Truncated);
    }
    return m;
}

Decoded read_pong(Reader& r) {
    Pong m;
    if (!r.u32(m.ping_id) || !r.u64(m.ping_send_time_us) || !r.u64(m.receive_time_us) ||
        !r.u64(m.reply_time_us)) {
        return decode_failure(Error::Truncated);
    }
    return m;
}

Decoded read_peer_error(Reader& r) {
    PeerError m;
    if (!r.u16(m.code)) {
        return decode_failure(Error::Truncated);
    }
    return m;
}

Decoded read_bye(Reader& r) {
    std::uint8_t reason = 0;
    if (!r.u8(reason)) {
        return decode_failure(Error::Truncated);
    }
    return Bye{.reason = static_cast<ByeReason>(reason)};
}

Decoded read_media_setup(Reader& r) {
    MediaSetup m;
    std::span<const std::uint8_t> token;
    if (!r.u16(m.udp_port) || !r.bytes(kSessionTokenSize, token)) {
        return decode_failure(Error::Truncated);
    }
    std::ranges::copy(token, m.session_token.begin());
    return m;
}

Decoded read_body(std::uint8_t type, Reader& r) {
    switch (static_cast<ControlType>(type)) {
    case ControlType::Hello:
        return read_hello(r);
    case ControlType::HandshakeMessage:
        return read_handshake(r);
    case ControlType::PairingConfirm:
        return read_pairing_confirm(r);
    case ControlType::Capabilities:
        return read_capabilities(r);
    case ControlType::ViewerMonitors:
        return read_viewer_monitors(r);
    case ControlType::StreamConfig:
        return read_stream_config(r);
    case ControlType::StreamStart:
        return read_stream_only<StreamStart>(r);
    case ControlType::StreamPause:
        return read_stream_only<StreamPause>(r);
    case ControlType::StreamResume:
        return read_stream_only<StreamResume>(r);
    case ControlType::StreamStop:
        return read_stream_only<StreamStop>(r);
    case ControlType::KeyframeRequest:
        return read_keyframe_request(r);
    case ControlType::Nack:
        return read_nack(r);
    case ControlType::ReceiverReport:
        return read_receiver_report(r);
    case ControlType::Ping:
        return read_ping(r);
    case ControlType::Pong:
        return read_pong(r);
    case ControlType::Heartbeat:
        return Heartbeat{};
    case ControlType::Error:
        return read_peer_error(r);
    case ControlType::Bye:
        return read_bye(r);
    case ControlType::MediaSetup:
        return read_media_setup(r);
    }
    return decode_failure(Error::UnknownType);
}

// --- Encoding ------------------------------------------------------------------------------------

void write_text(Writer& w, const std::string& text) {
    w.u8(static_cast<std::uint8_t>(text.size()));
    w.bytes(bytes_of(text));
}

void write_body(Writer& w, const ControlMessage& message) {
    std::visit(Overloaded{
                   [&](const Hello& m) {
                       w.u8(static_cast<std::uint8_t>(m.role));
                       w.u8(static_cast<std::uint8_t>(m.platform));
                       w.u8(m.protocol_minor);
                       w.u8(0);
                       write_text(w, m.device_name);
                   },
                   [&](const HandshakeMessage& m) {
                       w.u8(m.step);
                       w.u8(0);
                       w.u16(static_cast<std::uint16_t>(m.payload.size()));
                       w.bytes(m.payload);
                   },
                   [&](const PairingConfirm& m) { w.u8(static_cast<std::uint8_t>(m.decision)); },
                   [&](const Capabilities& m) {
                       w.u32(m.codecs);
                       w.u8(m.max_streams);
                       w.u8(m.flags);
                       w.u16(m.max_width);
                       w.u16(m.max_height);
                       w.u16(m.max_refresh_hz);
                   },
                   [&](const ViewerMonitors& m) {
                       w.u8(static_cast<std::uint8_t>(m.monitors.size()));
                       for (const Monitor& monitor : m.monitors) {
                           w.u8(monitor.monitor_id);
                           w.u16(monitor.width_px);
                           w.u16(monitor.height_px);
                           w.u32(monitor.refresh_millihertz);
                           w.u16(monitor.physical_width_mm);
                           w.u16(monitor.physical_height_mm);
                           w.u16(monitor.scale_percent);
                           write_text(w, monitor.name);
                       }
                   },
                   [&](const StreamConfig& m) {
                       w.u8(m.stream_id);
                       w.u8(m.monitor_id);
                       w.u16(m.width);
                       w.u16(m.height);
                       w.u32(m.refresh_millihertz);
                       w.u8(static_cast<std::uint8_t>(m.codec));
                       w.u8(static_cast<std::uint8_t>(m.profile));
                       w.u8(static_cast<std::uint8_t>(m.cursor_mode));
                       w.u8(0);
                       w.u32(m.bitrate_kbps);
                   },
                   [&](const StreamStart& m) { w.u8(m.stream_id); },
                   [&](const StreamPause& m) { w.u8(m.stream_id); },
                   [&](const StreamResume& m) { w.u8(m.stream_id); },
                   [&](const StreamStop& m) { w.u8(m.stream_id); },
                   [&](const KeyframeRequest& m) {
                       w.u8(m.stream_id);
                       w.u8(static_cast<std::uint8_t>(m.reason));
                   },
                   [&](const Nack& m) {
                       w.u8(m.stream_id);
                       w.u32(m.frame_id);
                       w.u8(static_cast<std::uint8_t>(m.ranges.size()));
                       for (const NackRange& range : m.ranges) {
                           w.u16(range.first_fragment);
                           w.u16(range.count);
                       }
                   },
                   [&](const ReceiverReport& m) {
                       w.u8(m.stream_id);
                       w.u8(0);
                       w.u16(m.loss_permille);
                       w.u32(m.frames_received);
                       w.u32(m.frames_dropped);
                       w.u32(m.decode_time_us);
                       w.u32(m.jitter_us);
                   },
                   [&](const Ping& m) {
                       w.u32(m.ping_id);
                       w.u64(m.send_time_us);
                   },
                   [&](const Pong& m) {
                       w.u32(m.ping_id);
                       w.u64(m.ping_send_time_us);
                       w.u64(m.receive_time_us);
                       w.u64(m.reply_time_us);
                   },
                   [&](const Heartbeat& /*m*/) {},
                   [&](const PeerError& m) { w.u16(m.code); },
                   [&](const Bye& m) { w.u8(static_cast<std::uint8_t>(m.reason)); },
                   [&](const MediaSetup& m) {
                       w.u16(m.udp_port);
                       w.bytes(m.session_token);
                   },
               },
               message);
}

} // namespace

ControlType type_of(const ControlMessage& message) {
    return std::visit([](const auto& m) { return std::remove_cvref_t<decltype(m)>::kType; },
                      message);
}

Expected<ControlMessage, Error> decode_control(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > kMaxControlMessageSize) {
        return decode_failure(Error::TooLarge);
    }
    Reader reader(bytes);
    std::uint8_t magic0 = 0;
    std::uint8_t magic1 = 0;
    std::uint8_t version = 0;
    if (!reader.u8(magic0) || !reader.u8(magic1)) {
        return decode_failure(Error::Truncated);
    }
    if (magic0 != kMagic[0] || magic1 != kMagic[1]) {
        return decode_failure(Error::BadMagic);
    }
    if (!reader.u8(version)) {
        return decode_failure(Error::Truncated);
    }
    if (version != kVersion) {
        return decode_failure(Error::UnsupportedVersion);
    }
    std::uint8_t type = 0;
    if (!reader.u8(type)) {
        return decode_failure(Error::Truncated);
    }

    auto decoded = read_body(type, reader);
    if (!decoded) {
        return decoded;
    }
    if (reader.remaining() != 0) {
        return decode_failure(Error::TrailingBytes);
    }
    const Check valid = std::visit([](const auto& m) { return validate(m); }, decoded.value());
    if (!valid) {
        return decode_failure(valid.error());
    }
    return decoded;
}

Expected<std::vector<std::uint8_t>, Error> encode_control(const ControlMessage& message) {
    using Result = Expected<std::vector<std::uint8_t>, Error>;
    const Check valid = std::visit([](const auto& m) { return validate(m); }, message);
    if (!valid) {
        return Result::failure(valid.error());
    }
    std::array<std::uint8_t, kMaxControlMessageSize> buffer{};
    Writer writer(buffer);
    writer.bytes(kMagic);
    writer.u8(kVersion);
    writer.u8(static_cast<std::uint8_t>(type_of(message)));
    write_body(writer, message);
    if (!writer.ok()) {
        return Result::failure(Error::TooLarge);
    }
    return std::vector<std::uint8_t>(buffer.begin(),
                                     buffer.begin() + static_cast<std::ptrdiff_t>(writer.size()));
}

} // namespace osplay::protocol
