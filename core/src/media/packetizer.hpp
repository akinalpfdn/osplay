#pragma once

#include "media/access_unit.hpp"
#include "protocol/media_packets.hpp"
#include "util/error.hpp"
#include "util/expected.hpp"

#include <cstdint>
#include <vector>

namespace osplay::media {

/// Splits access units into VideoFragments of at most kMaxVideoPayload bytes, every fragment but
/// the last full (docs/PROTOCOL.md, VideoFragment). Fragments are views into the access unit.
class Packetizer {
public:
    /// Assigns the next frame ID and writes every fragment of `unit` into `out` (replacing its
    /// contents; its capacity is reused, so steady-state packetizing does not allocate).
    [[nodiscard]] Expected<std::uint32_t, Error>
    packetize(const AccessUnit& unit, std::vector<protocol::VideoFragment>& out);

    /// Fragments [first, first + count) of an already numbered frame, for retransmission; the
    /// range is clamped to the frame's fragments.
    static void fragments(std::uint32_t frame_id, const AccessUnit& unit, std::uint16_t first,
                          std::uint16_t count, std::vector<protocol::VideoFragment>& out);

    [[nodiscard]] static std::uint16_t fragment_count(std::size_t size);

private:
    std::uint32_t next_frame_id_ = 0;
};

} // namespace osplay::media
