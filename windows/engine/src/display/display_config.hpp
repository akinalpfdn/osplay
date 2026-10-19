#pragma once

// Queries of the active display configuration through the CCD API, shared by the virtual monitor
// lookup and the viewer's monitor enumeration.

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace osplay::engine {

/// Every active path, retried while the configuration changes under the query.
[[nodiscard]] std::optional<std::vector<DISPLAYCONFIG_PATH_INFO>> active_display_paths();

/// GDI name (\\.\DISPLAYn) of the path's source.
[[nodiscard]] std::optional<std::wstring> source_gdi_name(const DISPLAYCONFIG_PATH_INFO& path);

/// The monitor's EDID name ("DELL U2720Q"); empty when the monitor reports none.
[[nodiscard]] std::wstring target_friendly_name(const DISPLAYCONFIG_PATH_INFO& path);

} // namespace osplay::engine
