#include "media/h264_stream.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace osplay::engine {
namespace {

constexpr std::array<std::uint8_t, 4> kStartCode{0, 0, 0, 1};

std::uint8_t type_of(std::span<const std::uint8_t> unit) {
    return unit.empty() ? 0 : static_cast<std::uint8_t>(unit[0] & 0x1FU);
}

void append_unit(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> unit) {
    out.insert(out.end(), kStartCode.begin(), kStartCode.end());
    out.insert(out.end(), unit.begin(), unit.end());
}

} // namespace

std::vector<std::span<const std::uint8_t>> nal_units(std::span<const std::uint8_t> annexb) {
    struct Start {
        std::size_t code;
        std::size_t payload;
    };
    std::vector<Start> starts;
    for (std::size_t i = 0; i + 3 <= annexb.size(); ++i) {
        if (annexb[i] == 0 && annexb[i + 1] == 0 && annexb[i + 2] == 1) {
            starts.push_back({.code = i > 0 && annexb[i - 1] == 0 ? i - 1 : i, .payload = i + 3});
            i += 2;
        }
    }
    if (starts.empty() ||
        !std::all_of(annexb.begin(), annexb.begin() + static_cast<std::ptrdiff_t>(starts[0].code),
                     [](std::uint8_t byte) { return byte == 0; })) {
        return {};
    }
    std::vector<std::span<const std::uint8_t>> units;
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const std::size_t end = index + 1 < starts.size() ? starts[index + 1].code : annexb.size();
        if (end > starts[index].payload) {
            units.push_back(annexb.subspan(starts[index].payload, end - starts[index].payload));
        }
    }
    return units;
}

void ParameterSetCache::remember(std::span<const std::uint8_t> annexb) {
    for (const auto unit : nal_units(annexb)) {
        if (type_of(unit) == kNalSps) {
            sps_.assign(unit.begin(), unit.end());
        } else if (type_of(unit) == kNalPps) {
            pps_.assign(unit.begin(), unit.end());
        }
    }
}

std::vector<std::uint8_t> ParameterSetCache::prepare(std::span<const std::uint8_t> annexb,
                                                     bool& keyframe) {
    const auto units = nal_units(annexb);
    keyframe = std::ranges::any_of(units, [](auto unit) { return type_of(unit) == kNalIdr; });
    const bool has_sps =
        std::ranges::any_of(units, [](auto unit) { return type_of(unit) == kNalSps; });
    remember(annexb);
    std::vector<std::uint8_t> out;
    out.reserve(annexb.size() + sps_.size() + pps_.size() + (2 * kStartCode.size()));
    if (keyframe && !has_sps && !sps_.empty() && !pps_.empty()) {
        append_unit(out, sps_);
        append_unit(out, pps_);
    }
    for (const auto unit : units) {
        append_unit(out, unit);
    }
    return out;
}

} // namespace osplay::engine
