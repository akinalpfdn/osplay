#pragma once

// Windows' display scale ("Scale" in Settings › Display) as the CCD API reports it: in steps
// relative to the scale Windows recommends for the monitor, not in percent. The steps are the ones
// Settings offers; the API itself is undocumented (DECISIONS.md, 2026-11-01).

#include <array>
#include <cstdint>
#include <optional>

namespace osplay::engine {

inline constexpr std::array<std::uint16_t, 12> kWindowsScaleSteps{100, 125, 150, 175, 200, 225,
                                                                  250, 300, 350, 400, 450, 500};

/// A monitor's scale range, in steps from its recommended scale (min_relative is 0 or negative:
/// 100% is that many steps below the recommendation).
struct DpiScaleRange {
    std::int32_t min_relative = 0;
    std::int32_t current_relative = 0;
    std::int32_t max_relative = 0;
};

/// The relative step that sets the monitor closest to `percent` (a viewer's backing scale × 100),
/// within its range; nullopt when the range does not fit the known steps (Windows changed them).
[[nodiscard]] std::optional<std::int32_t> relative_scale_step(std::uint16_t percent,
                                                              DpiScaleRange range);

} // namespace osplay::engine
