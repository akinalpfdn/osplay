#pragma once

#include <osplay/win/helper_protocol.hpp>

#include <string>

namespace osplay::win {

/// Renders vdd_settings.xml for exactly one virtual monitor with exactly one mode.
///
/// The Virtual Display Driver reads this file only when its device starts, and uses the first
/// resolution as the preferred mode. Osplay owns the file: it is rewritten before every session.
/// `config` must satisfy helper::is_valid().
[[nodiscard]] std::string render_vdd_settings(const helper::MonitorConfig& config);

} // namespace osplay::win
