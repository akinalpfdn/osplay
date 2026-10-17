#include "media/packetizer.hpp"

#include <algorithm>

namespace osplay::media {

using protocol::kMaxFragmentsPerFrame;
using protocol::kMaxVideoPayload;

std::uint16_t Packetizer::fragment_count(std::size_t size) {
    return static_cast<std::uint16_t>((size + kMaxVideoPayload - 1) / kMaxVideoPayload);
}

Expected<std::uint32_t, Error> Packetizer::packetize(const AccessUnit& unit,
                                                     std::vector<protocol::VideoFragment>& out) {
    using Result = Expected<std::uint32_t, Error>;
    if (unit.data.empty() || unit.stream_id >= protocol::kMaxStreams ||
        (unit.codec_config && !unit.keyframe)) {
        return Result::failure(Error::InvalidArgument);
    }
    if (unit.data.size() > std::size_t{kMaxFragmentsPerFrame} * kMaxVideoPayload) {
        return Result::failure(Error::TooLarge);
    }
    const std::uint32_t frame_id = next_frame_id_++;
    fragments(frame_id, unit, 0, static_cast<std::uint16_t>(kMaxFragmentsPerFrame), out);
    return frame_id;
}

void Packetizer::fragments(std::uint32_t frame_id, const AccessUnit& unit, std::uint16_t first,
                           std::uint16_t count, std::vector<protocol::VideoFragment>& out) {
    out.clear();
    const std::uint16_t total = fragment_count(unit.data.size());
    const std::uint32_t end = std::min<std::uint32_t>(std::uint32_t{first} + count, total);
    std::uint8_t flags = 0;
    if (unit.keyframe) {
        flags |= protocol::kVideoKeyframe;
    }
    if (unit.codec_config) {
        flags |= protocol::kVideoCodecConfig;
    }
    for (std::uint32_t index = first; index < end; ++index) {
        const std::size_t offset = index * kMaxVideoPayload;
        out.push_back(protocol::VideoFragment{
            .stream_id = unit.stream_id,
            .flags = flags,
            .frame_id = frame_id,
            .fragment_index = static_cast<std::uint16_t>(index),
            .fragment_count = total,
            .capture_time_us = unit.capture_time_us,
            .payload =
                unit.data.subspan(offset, std::min(kMaxVideoPayload, unit.data.size() - offset)),
        });
    }
}

} // namespace osplay::media
