#pragma once

// The viewer's physical monitors on Windows: CCD for names and exact refresh rates, GDI for the
// current mode and physical size, the shell's effective DPI for scaling. The Virtual Display
// Driver's monitor is never offered.

#include "viewer/monitor_layout.hpp"

#include <vector>

namespace osplay::engine {

/// Active physical monitors, rectangles and sizes in physical pixels.
[[nodiscard]] std::vector<MonitorCandidate> active_physical_monitors();

} // namespace osplay::engine
