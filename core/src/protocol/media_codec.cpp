#include "protocol/byte_io.hpp"
#include "protocol/media_packets.hpp"

#include <algorithm>
#include <type_traits>

namespace osplay::protocol {
namespace {

using Check = Status<Error>;
using Decoded = Expected<MediaBody, Error>;

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
bool valid_stream(std::uint8_t stream_id) {
    return stream_id < kMaxStreams;
}

Check validate(const UdpHello& /*m*/) {
    return ok();
}

Check validate(const VideoFragment& m) {
    const auto known_flags = static_cast<std::uint8_t>(kVideoKeyframe | kVideoCodecConfig);
    const bool flags_ok = (m.flags & static_cast<std::uint8_t>(~known_flags)) == 0 &&
                          ((m.flags & kVideoCodecConfig) == 0 || (m.flags & kVideoKeyframe) != 0);
    if (!valid_stream(m.stream_id) || !flags_ok || m.fragment_count == 0 ||
        m.fragment_count > kMaxFragmentsPerFrame || m.fragment_index >= m.fragment_count ||
        m.payload.empty() || m.payload.size() > kMaxVideoPayload) {
        return fail(Error::ValueOutOfRange);
    }
    // Every fragment but the last is full, so a receiver can place it without knowing the frame
    // size.
    const bool last = m.fragment_index + 1 == m.fragment_count;
    if (!last && m.payload.size() != kMaxVideoPayload) {
        return fail(Error::ValueOutOfRange);
    }
    return ok();
}

Check validate(const CursorPosition& m) {
    return valid_stream(m.stream_id) ? ok() : fail(Error::ValueOutOfRange);
}

Check validate(const CursorShapeFragment& m) {
    if (!valid_stream(m.stream_id) || m.width == 0 || m.width > kMaxCursorDimension ||
        m.height == 0 || m.height > kMaxCursorDimension || m.hotspot_x >= m.width ||
        m.hotspot_y >= m.height ||
        m.fragment_count != cursor_shape_fragment_count(m.width, m.height) ||
        m.fragment_index >= m.fragment_count ||
        m.payload.size() != cursor_shape_fragment_size(m.width, m.height, m.fragment_index)) {
        return fail(Error::ValueOutOfRange);
    }
    return ok();
}

Decoded read_udp_hello(Reader& r) {
    UdpHello m;
    std::span<const std::uint8_t> token;
    if (!r.bytes(kSessionTokenSize, token)) {
        return decode_failure(Error::Truncated);
    }
    std::ranges::copy(token, m.session_token.begin());
    return m;
}

Decoded read_video(Reader& r) {
    VideoFragment m;
    if (!r.u8(m.stream_id) || !r.u8(m.flags) || !r.u32(m.frame_id) || !r.u16(m.fragment_index) ||
        !r.u16(m.fragment_count) || !r.u64(m.capture_time_us) ||
        !r.bytes(r.remaining(), m.payload)) {
        return decode_failure(Error::Truncated);
    }
    return m;
}

Decoded read_cursor_position(Reader& r) {
    CursorPosition m;
    std::uint8_t visible = 0;
    if (!r.u8(m.stream_id) || !r.u8(visible) || !r.u32(m.shape_id) || !r.i32(m.x) || !r.i32(m.y) ||
        !r.u64(m.capture_time_us)) {
        return decode_failure(Error::Truncated);
    }
    if (visible > 1) {
        return decode_failure(Error::ValueOutOfRange);
    }
    m.visible = visible == 1;
    return m;
}

Decoded read_cursor_shape(Reader& r) {
    CursorShapeFragment m;
    if (!r.u8(m.stream_id) || !r.u32(m.shape_id) || !r.u16(m.width) || !r.u16(m.height) ||
        !r.u16(m.hotspot_x) || !r.u16(m.hotspot_y) || !r.u16(m.fragment_index) ||
        !r.u16(m.fragment_count) || !r.bytes(r.remaining(), m.payload)) {
        return decode_failure(Error::Truncated);
    }
    return m;
}

Decoded read_body(std::uint8_t type, Reader& r) {
    switch (static_cast<MediaType>(type)) {
    case MediaType::UdpHello:
        return read_udp_hello(r);
    case MediaType::VideoFragment:
        return read_video(r);
    case MediaType::CursorPosition:
        return read_cursor_position(r);
    case MediaType::CursorShapeFragment:
        return read_cursor_shape(r);
    }
    return decode_failure(Error::UnknownType);
}

void write_body(Writer& w, const MediaBody& body) {
    std::visit(Overloaded{
                   [&](const UdpHello& m) { w.bytes(m.session_token); },
                   [&](const VideoFragment& m) {
                       w.u8(m.stream_id);
                       w.u8(m.flags);
                       w.u32(m.frame_id);
                       w.u16(m.fragment_index);
                       w.u16(m.fragment_count);
                       w.u64(m.capture_time_us);
                       w.bytes(m.payload);
                   },
                   [&](const CursorPosition& m) {
                       w.u8(m.stream_id);
                       w.u8(m.visible ? 1 : 0);
                       w.u32(m.shape_id);
                       w.i32(m.x);
                       w.i32(m.y);
                       w.u64(m.capture_time_us);
                   },
                   [&](const CursorShapeFragment& m) {
                       w.u8(m.stream_id);
                       w.u32(m.shape_id);
                       w.u16(m.width);
                       w.u16(m.height);
                       w.u16(m.hotspot_x);
                       w.u16(m.hotspot_y);
                       w.u16(m.fragment_index);
                       w.u16(m.fragment_count);
                       w.bytes(m.payload);
                   },
               },
               body);
}

} // namespace

bool VideoFragment::operator==(const VideoFragment& other) const {
    return stream_id == other.stream_id && flags == other.flags && frame_id == other.frame_id &&
           fragment_index == other.fragment_index && fragment_count == other.fragment_count &&
           capture_time_us == other.capture_time_us && std::ranges::equal(payload, other.payload);
}

bool CursorShapeFragment::operator==(const CursorShapeFragment& other) const {
    return stream_id == other.stream_id && shape_id == other.shape_id && width == other.width &&
           height == other.height && hotspot_x == other.hotspot_x && hotspot_y == other.hotspot_y &&
           fragment_index == other.fragment_index && fragment_count == other.fragment_count &&
           std::ranges::equal(payload, other.payload);
}

MediaType type_of(const MediaPacket& packet) {
    return std::visit([](const auto& m) { return std::remove_cvref_t<decltype(m)>::kType; },
                      packet.body);
}

std::uint16_t cursor_shape_fragment_count(std::uint16_t width, std::uint16_t height) {
    const std::size_t total = std::size_t{width} * height * 4;
    return static_cast<std::uint16_t>((total + kMaxCursorShapePayload - 1) /
                                      kMaxCursorShapePayload);
}

std::size_t cursor_shape_fragment_size(std::uint16_t width, std::uint16_t height,
                                       std::uint16_t fragment_index) {
    const std::size_t total = std::size_t{width} * height * 4;
    const std::uint16_t count = cursor_shape_fragment_count(width, height);
    if (count == 0 || fragment_index >= count) {
        return 0;
    }
    if (fragment_index + 1 < count) {
        return kMaxCursorShapePayload;
    }
    return total - (kMaxCursorShapePayload * (count - 1U));
}

Expected<MediaPacket, Error> decode_media(std::span<const std::uint8_t> bytes) {
    using Result = Expected<MediaPacket, Error>;
    if (bytes.size() > kMaxMediaPlaintextSize) {
        return Result::failure(Error::TooLarge);
    }
    Reader reader(bytes);
    std::uint8_t magic0 = 0;
    std::uint8_t magic1 = 0;
    std::uint8_t version = 0;
    if (!reader.u8(magic0) || !reader.u8(magic1)) {
        return Result::failure(Error::Truncated);
    }
    if (magic0 != kMagic[0] || magic1 != kMagic[1]) {
        return Result::failure(Error::BadMagic);
    }
    if (!reader.u8(version)) {
        return Result::failure(Error::Truncated);
    }
    if (version != kVersion) {
        return Result::failure(Error::UnsupportedVersion);
    }
    std::uint8_t type = 0;
    std::uint64_t sequence = 0;
    if (!reader.u8(type) || !reader.u64(sequence)) {
        return Result::failure(Error::Truncated);
    }

    auto body = read_body(type, reader);
    if (!body) {
        return Result::failure(body.error());
    }
    if (reader.remaining() != 0) {
        return Result::failure(Error::TrailingBytes);
    }
    const Check valid = std::visit([](const auto& m) { return validate(m); }, body.value());
    if (!valid) {
        return Result::failure(valid.error());
    }
    return MediaPacket{.sequence = sequence, .body = body.value()};
}

Expected<std::size_t, Error> encode_media(const MediaPacket& packet, std::span<std::uint8_t> out) {
    using Result = Expected<std::size_t, Error>;
    const Check valid = std::visit([](const auto& m) { return validate(m); }, packet.body);
    if (!valid) {
        return Result::failure(valid.error());
    }
    Writer writer(out);
    writer.bytes(kMagic);
    writer.u8(kVersion);
    writer.u8(static_cast<std::uint8_t>(type_of(packet)));
    writer.u64(packet.sequence);
    write_body(writer, packet.body);
    if (!writer.ok()) {
        return Result::failure(Error::BufferTooSmall);
    }
    return writer.size();
}

} // namespace osplay::protocol
