#pragma once

#include "virtual_display_provider.hpp"

#include <string>
#include <vector>

namespace osplay::engine {

/// GDI names (\\.\DISPLAYn) of the Virtual Display Driver's active monitors, through the CCD
/// API: device by device in instance ID order, and by target (the driver's connector index) within
/// a device. MonitorNotFound while none has joined the desktop yet.
[[nodiscard]] win::Expected<std::vector<std::wstring>, DisplayError> vdd_gdi_device_names();

} // namespace osplay::engine
