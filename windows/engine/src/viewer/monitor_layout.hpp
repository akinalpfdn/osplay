#pragma once

// Turns the monitors Windows reports into the ones the viewer offers (DEVPLAN Phase 9): values the
// protocol accepts, ids by desktop position, names the protocol can carry, and, after a display
// change, which of the offered monitors are still attached. Portable and unit tested.

#include "viewer/viewer_stream.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace osplay::engine {

/// One active physical monitor as the platform reports it.
struct MonitorCandidate {
    std::string key; ///< stable while it stays attached (the GDI device name)
    MonitorRect rect;
    std::uint32_t width_px = 0;
    std::uint32_t height_px = 0;
    std::uint32_t refresh_millihertz = 0; ///< 0 when unknown
    std::uint32_t physical_width_mm = 0;  ///< 0 when unknown
    std::uint32_t physical_height_mm = 0;
    std::uint32_t dpi = 96;
    std::string name_utf8;
};

struct OfferedMonitor {
    ViewerMonitor monitor; ///< `description.name` is null; use `name`
    std::string name;
    std::string key;
};

/// At most OSPLAY_MAX_MONITORS monitors, left to right then top to bottom, numbered from 0.
[[nodiscard]] std::vector<OfferedMonitor> offer_monitors(std::vector<MonitorCandidate> candidates);

/// The ids of the offered monitors whose key is still among the active ones.
[[nodiscard]] std::set<std::uint8_t> still_attached(const std::vector<OfferedMonitor>& offered,
                                                    const std::vector<MonitorCandidate>& active);

/// Whether an offered monitor that is still attached now runs another mode (resolution, refresh
/// rate or scaling), so the host has to make its display again.
[[nodiscard]] bool mode_changed(const std::vector<OfferedMonitor>& offered,
                                const std::vector<MonitorCandidate>& active);

/// Control characters dropped and the result cut at a character boundary to the protocol's
/// 64-byte limit. Invalid UTF-8 ends the name.
[[nodiscard]] std::string protocol_name(std::string_view utf8);

} // namespace osplay::engine
