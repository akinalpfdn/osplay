#include "media/stream_size.hpp"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <utility>

namespace osplay::media {
namespace {

constexpr std::uint64_t kMaxStreamSide = 8192; // StreamConfig's limit (PROTOCOL.md)

/// `long_side` × `short_side` scaled to fit `max_long` × `max_short`, or unchanged when it fits.
/// Integer arithmetic: a floating-point scale turns 2214 × (1080 / 2214) into 1079.99.
std::pair<std::uint64_t, std::uint64_t> fit(std::uint64_t long_side, std::uint64_t short_side,
                                            std::uint64_t max_long, std::uint64_t max_short) {
    if (long_side <= max_long && short_side <= max_short) {
        return {long_side, short_side};
    }
    if (long_side * max_short >= short_side * max_long) {
        return {max_long, short_side * max_long / long_side}; // the long side limits it
    }
    return {long_side * max_short / short_side, max_short};
}

std::uint32_t even(std::uint64_t side) {
    return static_cast<std::uint32_t>(side & ~std::uint64_t{1});
}

} // namespace

StreamSize fit_stream_size(std::uint32_t monitor_width, std::uint32_t monitor_height,
                           std::uint32_t max_width, std::uint32_t max_height) {
    if (monitor_width == 0 || monitor_height == 0) {
        return {};
    }
    const bool landscape = monitor_width >= monitor_height;
    auto [long_side, short_side] =
        landscape ? std::pair<std::uint64_t, std::uint64_t>{monitor_width, monitor_height}
                  : std::pair<std::uint64_t, std::uint64_t>{monitor_height, monitor_width};
    if (max_width != 0 && max_height != 0) {
        const std::uint64_t box_long = std::max(max_width, max_height);
        const std::uint64_t box_short = std::min(max_width, max_height);
        std::tie(long_side, short_side) = fit(long_side, short_side, box_long, box_short);
    }
    std::tie(long_side, short_side) = fit(long_side, short_side, kMaxStreamSide, kMaxStreamSide);
    return landscape ? StreamSize{.width = even(long_side), .height = even(short_side)}
                     : StreamSize{.width = even(short_side), .height = even(long_side)};
}

} // namespace osplay::media
