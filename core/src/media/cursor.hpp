#pragma once

// Cursor shapes travel as BGRA images split into CursorShapeFragments (docs/PROTOCOL.md); the host
// splits them here and the viewer reassembles them. Positions need no processing.

#include "protocol/media_packets.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace osplay::media {

/// One cursor image: premultiplied BGRA, rows top to bottom, no padding.
struct CursorShape {
    std::uint8_t stream_id = 0;
    std::uint32_t shape_id = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t hotspot_x = 0;
    std::uint16_t hotspot_y = 0;
    std::span<const std::uint8_t> bgra; // width × height × 4 bytes
};

/// Number of fragments for an image of this size.
[[nodiscard]] std::uint16_t cursor_fragment_count(std::uint16_t width, std::uint16_t height);

/// Replaces `out` with the shape's fragments; their payloads view `shape.bgra`.
[[nodiscard]] Status<Error> split_cursor_shape(const CursorShape& shape,
                                               std::vector<protocol::CursorShapeFragment>& out);

/// Rebuilds one stream's shapes. A fragment of a different shape abandons the one in progress, so
/// only the newest shape is ever assembled.
class CursorShapeAssembler {
public:
    /// The completed shape once its last missing fragment arrived; its image is valid until the
    /// next call. Inconsistent fragments are ignored.
    std::optional<CursorShape> add(const protocol::CursorShapeFragment& fragment);

    /// The last shape delivered, if any.
    [[nodiscard]] std::optional<std::uint32_t> completed_shape() const { return completed_shape_; }

private:
    std::optional<protocol::CursorShapeFragment> current_; // header of the shape in progress
    std::vector<std::uint8_t> image_;
    std::vector<bool> have_;
    std::uint16_t received_ = 0;
    std::optional<std::uint32_t> completed_shape_;
};

} // namespace osplay::media
