#pragma once

#include <cstdint>
#include <span>

namespace osplay::media {

/// One encoded frame (H.264 Annex B access unit) as the host's encoder produced it.
struct AccessUnit {
    std::uint8_t stream_id = 0;
    bool keyframe = false;
    bool codec_config = false; // SPS/PPS in-band; only with keyframes
    std::uint64_t capture_time_us = 0;
    std::span<const std::uint8_t> data;
};

} // namespace osplay::media
