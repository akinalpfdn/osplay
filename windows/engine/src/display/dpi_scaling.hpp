#pragma once

#include "virtual_display_provider.hpp"

#include <osplay/win/expected.hpp>

#include <cstdint>
#include <string>

namespace osplay::engine {

/// Sets the display scale of the monitor shown by `gdi_name` (\\.\DISPLAYn) to the Windows step
/// closest to `percent`, through the CCD API's undocumented scale requests (DECISIONS.md,
/// 2026-11-01). MonitorNotFound when no active path shows it; ModeChangeFailed when Windows
/// refuses the requests or reports a range outside the known steps.
[[nodiscard]] win::Status<DisplayError> set_display_scale(const std::wstring& gdi_name,
                                                          std::uint16_t percent);

} // namespace osplay::engine
