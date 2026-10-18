#include "viewer/monitor_layout.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace osplay::engine {
namespace {

constexpr std::size_t kMaxNameBytes = 64; // protocol: kMaxStringBytes
constexpr std::uint32_t kMinWidth = 320;
constexpr std::uint32_t kMinHeight = 240;
constexpr std::uint32_t kMaxSide = 16384;
constexpr std::uint32_t kMinRefresh = 1000;
constexpr std::uint32_t kMaxRefresh = 500'000;
constexpr std::uint32_t kDefaultRefresh = 60'000;
constexpr std::uint32_t kMaxPhysicalMm = 10'000;
constexpr std::uint32_t kBaseDpi = 96;
constexpr std::uint32_t kMinScale = 100;
constexpr std::uint32_t kMaxScale = 400;

/// Length of the UTF-8 sequence starting with `lead`, or 0 when it cannot start one.
std::size_t sequence_length(std::uint8_t lead) {
    if (lead < 0x80U) {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 0;
}

/// The code point of a well-formed sequence.
std::optional<std::uint32_t> decode(std::string_view sequence) {
    constexpr std::array<std::uint32_t, 5> kSmallest{0, 0, 0x80, 0x800, 0x10000};
    constexpr std::array<std::uint8_t, 5> kLeadMask{0, 0x7F, 0x1F, 0x0F, 0x07};
    const std::size_t length = sequence.size();
    std::uint32_t code_point = static_cast<std::uint8_t>(sequence[0]) & kLeadMask.at(length);
    for (std::size_t i = 1; i < length; ++i) {
        const auto continuation = static_cast<std::uint8_t>(sequence[i]);
        if ((continuation & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        code_point = (code_point << 6U) | (continuation & 0x3FU);
    }
    const bool surrogate = code_point >= 0xD800U && code_point <= 0xDFFFU;
    if (code_point < kSmallest.at(length) || surrogate || code_point > 0x10FFFFU) {
        return std::nullopt;
    }
    return code_point;
}

bool is_control(std::uint32_t code_point) {
    return code_point < 0x20U || (code_point >= 0x7FU && code_point < 0xA0U);
}

std::uint16_t clamp16(std::uint32_t value, std::uint32_t low, std::uint32_t high) {
    return static_cast<std::uint16_t>(std::clamp(value, low, high));
}

} // namespace

std::string protocol_name(std::string_view utf8) {
    std::string name;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const std::size_t length = sequence_length(static_cast<std::uint8_t>(utf8[i]));
        if (length == 0 || i + length > utf8.size()) {
            break;
        }
        const std::string_view sequence = utf8.substr(i, length);
        const auto code_point = decode(sequence);
        if (!code_point) {
            break;
        }
        if (!is_control(*code_point)) {
            if (name.size() + length > kMaxNameBytes) {
                break;
            }
            name.append(sequence);
        }
        i += length;
    }
    return name;
}

std::vector<OfferedMonitor> offer_monitors(std::vector<MonitorCandidate> candidates) {
    std::erase_if(candidates, [](const MonitorCandidate& c) {
        return c.width_px < kMinWidth || c.height_px < kMinHeight || c.width_px > kMaxSide ||
               c.height_px > kMaxSide;
    });
    std::ranges::sort(candidates, [](const MonitorCandidate& a, const MonitorCandidate& b) {
        return std::pair(a.rect.left, a.rect.top) < std::pair(b.rect.left, b.rect.top);
    });
    if (candidates.size() > OSPLAY_MAX_MONITORS) {
        candidates.resize(OSPLAY_MAX_MONITORS);
    }
    std::vector<OfferedMonitor> offered;
    offered.reserve(candidates.size());
    std::uint8_t next_id = 0;
    for (MonitorCandidate& candidate : candidates) {
        const std::uint32_t refresh =
            candidate.refresh_millihertz == 0
                ? kDefaultRefresh
                : std::clamp(candidate.refresh_millihertz, kMinRefresh, kMaxRefresh);
        const std::uint32_t scale = ((candidate.dpi * kMinScale) + (kBaseDpi / 2)) / kBaseDpi;
        OfferedMonitor monitor{
            .monitor = {.id = next_id,
                        .rect = candidate.rect,
                        .description = {.monitor_id = next_id,
                                        .width_px = static_cast<std::uint16_t>(candidate.width_px),
                                        .height_px =
                                            static_cast<std::uint16_t>(candidate.height_px),
                                        .refresh_millihertz = refresh,
                                        .physical_width_mm =
                                            clamp16(candidate.physical_width_mm, 0, kMaxPhysicalMm),
                                        .physical_height_mm = clamp16(candidate.physical_height_mm,
                                                                      0, kMaxPhysicalMm),
                                        .scale_percent = clamp16(scale, kMinScale, kMaxScale),
                                        .name = nullptr}},
            .name = protocol_name(candidate.name_utf8),
            .key = std::move(candidate.key),
        };
        offered.push_back(std::move(monitor));
        ++next_id;
    }
    return offered;
}

std::set<std::uint8_t> still_attached(const std::vector<OfferedMonitor>& offered,
                                      const std::vector<MonitorCandidate>& active) {
    std::set<std::uint8_t> attached;
    for (const OfferedMonitor& monitor : offered) {
        if (std::ranges::any_of(active,
                                [&](const MonitorCandidate& c) { return c.key == monitor.key; })) {
            attached.insert(monitor.monitor.id);
        }
    }
    return attached;
}

} // namespace osplay::engine
