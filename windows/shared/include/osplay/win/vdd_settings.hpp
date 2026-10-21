#pragma once

#include <osplay/win/helper_protocol.hpp>

#include <string>
#include <vector>

namespace osplay::win {

/// Renders vdd_settings.xml for these virtual monitors: their count and every distinct mode, the
/// first monitor's mode first.
///
/// The Virtual Display Driver reads this file only when its device starts. Every monitor offers
/// every listed mode and starts in the first one; the engine then sets each monitor's own mode.
/// Osplay owns the file: it is rewritten before every session. `monitors` must satisfy
/// helper::is_valid().
[[nodiscard]] std::string render_vdd_settings(const std::vector<helper::MonitorConfig>& monitors);

} // namespace osplay::win
