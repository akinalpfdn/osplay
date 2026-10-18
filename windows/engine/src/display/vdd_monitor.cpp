#include "vdd_monitor.hpp"

#include "display_config.hpp"

#include <osplay/win/vdd_device_lookup.hpp>

#include <windows.h>

#include <cwchar>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace osplay::engine {
namespace {

/// The adapter's device interface path starts with its instance ID in this form:
/// ROOT\DISPLAY\0000 -> \\?\ROOT#DISPLAY#0000#
std::wstring interface_path_prefix(const std::wstring& instance) {
    std::wstring prefix = L"\\\\?\\";
    for (const wchar_t c : instance) {
        prefix.push_back(c == L'\\' ? L'#' : c);
    }
    prefix.push_back(L'#');
    return prefix;
}

/// GDI name (\\.\DISPLAYn) of the active display driven by the adapter with this interface prefix.
std::optional<std::wstring> find_gdi_device_name(const std::wstring& prefix) {
    const auto paths = active_display_paths();
    if (!paths) {
        return std::nullopt;
    }
    for (const DISPLAYCONFIG_PATH_INFO& path : *paths) {
        DISPLAYCONFIG_ADAPTER_NAME adapter{
            .header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME,
                       .size = static_cast<UINT32>(sizeof(DISPLAYCONFIG_ADAPTER_NAME)),
                       .adapterId = path.sourceInfo.adapterId,
                       .id = 0},
            .adapterDevicePath = {},
        };
        if (DisplayConfigGetDeviceInfo(&adapter.header) != ERROR_SUCCESS ||
            _wcsnicmp(std::data(adapter.adapterDevicePath), prefix.c_str(), prefix.size()) != 0) {
            continue;
        }
        if (auto name = source_gdi_name(path)) {
            return name;
        }
    }
    return std::nullopt;
}

} // namespace

win::Expected<std::wstring, DisplayError> vdd_gdi_device_name() {
    using Result = win::Expected<std::wstring, DisplayError>;
    const auto device = win::find_device(win::kVddHardwareId);
    if (!device) {
        return Result::failure(DisplayError::VddNotInstalled);
    }
    const auto instance = win::instance_id(*device);
    if (!instance) {
        return Result::failure(DisplayError::MonitorNotFound);
    }
    auto name = find_gdi_device_name(interface_path_prefix(*instance));
    if (!name) {
        return Result::failure(DisplayError::MonitorNotFound);
    }
    return std::move(*name);
}

} // namespace osplay::engine
