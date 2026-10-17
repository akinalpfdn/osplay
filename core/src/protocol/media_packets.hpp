#pragma once

// Media channel packets (docs/PROTOCOL.md, "Media channel"). Decoding never allocates: payloads
// are views into the input buffer and stay valid only as long as it does.

#include "protocol/constants.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/error.hpp"
#include "util/expected.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace osplay::protocol {

enum class MediaType : std::uint8_t {
    UdpHello = 0x01,
    VideoFragment = 0x02,
    CursorPosition = 0x03,
    CursorShapeFragment = 0x04,
};

inline constexpr std::uint8_t kVideoKeyframe = 1U << 0U;
inline constexpr std::uint8_t kVideoCodecConfig = 1U << 1U;

struct UdpHello {
    static constexpr MediaType kType = MediaType::UdpHello;
    SessionToken session_token{};
    bool operator==(const UdpHello&) const = default;
};

struct VideoFragment {
    static constexpr MediaType kType = MediaType::VideoFragment;
    std::uint8_t stream_id = 0;
    std::uint8_t flags = 0;
    std::uint32_t frame_id = 0;
    std::uint16_t fragment_index = 0;
    std::uint16_t fragment_count = 0;
    std::uint64_t capture_time_us = 0;
    std::span<const std::uint8_t> payload;
    bool operator==(const VideoFragment& other) const;
};

struct CursorPosition {
    static constexpr MediaType kType = MediaType::CursorPosition;
    std::uint8_t stream_id = 0;
    bool visible = false;
    std::uint32_t shape_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint64_t capture_time_us = 0;
    bool operator==(const CursorPosition&) const = default;
};

struct CursorShapeFragment {
    static constexpr MediaType kType = MediaType::CursorShapeFragment;
    std::uint8_t stream_id = 0;
    std::uint32_t shape_id = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t hotspot_x = 0;
    std::uint16_t hotspot_y = 0;
    std::uint16_t fragment_index = 0;
    std::uint16_t fragment_count = 0;
    std::span<const std::uint8_t> payload;
    bool operator==(const CursorShapeFragment& other) const;
};

using MediaBody = std::variant<UdpHello, VideoFragment, CursorPosition, CursorShapeFragment>;

struct MediaPacket {
    std::uint64_t sequence = 0;
    MediaBody body;
    bool operator==(const MediaPacket&) const = default;
};

[[nodiscard]] MediaType type_of(const MediaPacket& packet);

/// Number of fragments a cursor shape of this size needs, and the payload size of each.
[[nodiscard]] std::uint16_t cursor_shape_fragment_count(std::uint16_t width, std::uint16_t height);
[[nodiscard]] std::size_t cursor_shape_fragment_size(std::uint16_t width, std::uint16_t height,
                                                     std::uint16_t fragment_index);

/// Decodes one plaintext media packet (header and body, without the AEAD tag).
[[nodiscard]] Expected<MediaPacket, Error> decode_media(std::span<const std::uint8_t> bytes);

/// Encodes one plaintext media packet into `out` and returns its size.
[[nodiscard]] Expected<std::size_t, Error> encode_media(const MediaPacket& packet,
                                                        std::span<std::uint8_t> out);

} // namespace osplay::protocol
