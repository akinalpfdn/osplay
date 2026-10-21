#pragma once

#include "virtual_display_provider.hpp"

#include <string>
#include <vector>

namespace osplay::engine {

/// GDI names (\\.\DISPLAYn) of the Virtual Display Driver's active monitors, through the CCD
/// API, ordered by target (the driver's connector index). MonitorNotFound while none has joined
/// the desktop yet.
[[nodiscard]] win::Expected<std::vector<std::wstring>, DisplayError> vdd_gdi_device_names();

} // namespace osplay::engine
