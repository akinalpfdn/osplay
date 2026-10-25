#pragma once

#include <osplay/win/helper_protocol.hpp>

#include <string>
#include <vector>

namespace osplay::win {

/// Renders vdd_settings.xml for these virtual monitors: one monitor per driver device and every
/// distinct mode, the first monitor's mode first.
///
/// The Virtual Display Driver reads this file only when a device starts, and every device reads
/// the same file. Each device drives one monitor (two on one device stall the whole desktop), which
/// offers every listed mode and starts in the first one; the engine then sets each monitor's own
/// mode. Osplay owns the file: it is rewritten before every session. `monitors` must satisfy
/// helper::is_valid().
[[nodiscard]] std::string render_vdd_settings(const std::vector<helper::MonitorConfig>& monitors);

} // namespace osplay::win
