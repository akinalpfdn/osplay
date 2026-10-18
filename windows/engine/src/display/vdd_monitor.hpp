#pragma once

#include "virtual_display_provider.hpp"

#include <string>

namespace osplay::engine {

/// GDI name (\\.\DISPLAYn) of the Virtual Display Driver's active monitor, through the CCD API.
/// MonitorNotFound while the monitor has not joined the desktop yet.
[[nodiscard]] win::Expected<std::wstring, DisplayError> vdd_gdi_device_name();

} // namespace osplay::engine
