#include "vdd_monitor.hpp"

#include "display_config.hpp"

#include <osplay/win/vdd_device_lookup.hpp>

#include <windows.h>

#include <algorithm>
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

/// GDI names (\\.\DISPLAYn) of the active displays driven by the adapter with this interface
/// prefix, ordered by target id.
std::vector<std::wstring> find_gdi_device_names(const std::wstring& prefix) {
    const auto paths = active_display_paths();
    if (!paths) {
        return {};
    }
    std::vector<std::pair<UINT32, std::wstring>> found;
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
            found.emplace_back(path.targetInfo.id, std::move(*name));
        }
    }
    std::ranges::sort(found, {}, &std::pair<UINT32, std::wstring>::first);
    std::vector<std::wstring> names;
    names.reserve(found.size());
    for (auto& entry : found) {
        names.push_back(std::move(entry.second));
    }
    return names;
}

} // namespace

win::Expected<std::vector<std::wstring>, DisplayError> vdd_gdi_device_names() {
    using Result = win::Expected<std::vector<std::wstring>, DisplayError>;
    const auto instances = win::device_instances(win::kVddHardwareId);
    if (instances.empty()) {
        return Result::failure(DisplayError::VddNotInstalled);
    }
    // The helper gives every monitor its own device and enables them in this order.
    std::vector<std::wstring> names;
    for (const std::wstring& instance : instances) {
        auto found = find_gdi_device_names(interface_path_prefix(instance));
        names.insert(names.end(), std::make_move_iterator(found.begin()),
                     std::make_move_iterator(found.end()));
    }
    if (names.empty()) {
        return Result::failure(DisplayError::MonitorNotFound);
    }
    return names;
}

} // namespace osplay::engine
