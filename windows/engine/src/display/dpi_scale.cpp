#include "dpi_scale.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <ranges>

namespace osplay::engine {

std::optional<std::int32_t> relative_scale_step(std::uint16_t percent, DpiScaleRange range) {
    constexpr auto kStepCount = static_cast<std::int32_t>(kWindowsScaleSteps.size());
    const std::int32_t recommended = -range.min_relative; // index of the recommended step
    const std::int32_t highest = recommended + range.max_relative;
    if (recommended < 0 || range.max_relative < 0 || highest >= kStepCount) {
        return std::nullopt;
    }
    const auto distance = [percent](std::int32_t index) {
        return std::abs(static_cast<int>(kWindowsScaleSteps.at(static_cast<std::size_t>(index))) -
                        static_cast<int>(percent));
    };
    const std::int32_t index = std::ranges::min(std::views::iota(0, kStepCount), {}, distance);
    return std::min(index, highest) - recommended;
}

} // namespace osplay::engine
