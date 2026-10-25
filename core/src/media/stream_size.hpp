#pragma once

// The size a host encodes a monitor at (DEVPLAN Phase 13c): the virtual display keeps the viewer
// monitor's resolution, and the stream is at most the size the user chose.

#include <cstdint>

namespace osplay::media {

struct StreamSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const StreamSize&, const StreamSize&) = default;
};

/// The monitor fitted inside `max_width` × `max_height` with its aspect ratio kept: the longer side
/// is measured against the longer side of the box, so a portrait monitor fits as well as a
/// landscape one. Never larger than the monitor, never larger than the protocol allows, both sides
/// even. A box of 0 × 0 means no limit.
StreamSize fit_stream_size(std::uint32_t monitor_width, std::uint32_t monitor_height,
                           std::uint32_t max_width, std::uint32_t max_height);

} // namespace osplay::media
