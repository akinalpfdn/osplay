#include "viewer/windows_monitors.hpp"

#include "display/display_config.hpp"
#include "display/vdd_monitor.hpp"

#include <windows.h>

#include <shellscalingapi.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace osplay::engine {
namespace {

std::string utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(),
                        length, nullptr, nullptr);
    return result;
}

/// Physical pixels for everything this thread asks, whatever the process declares.
class PhysicalPixelsScope {
public:
    PhysicalPixelsScope()
        : previous_(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalPixelsScope() {
        if (previous_ != nullptr) {
            SetThreadDpiAwarenessContext(previous_);
        }
    }
    PhysicalPixelsScope(const PhysicalPixelsScope&) = delete;
    PhysicalPixelsScope& operator=(const PhysicalPixelsScope&) = delete;
    PhysicalPixelsScope(PhysicalPixelsScope&&) = delete;
    PhysicalPixelsScope& operator=(PhysicalPixelsScope&&) = delete;

private:
    DPI_AWARENESS_CONTEXT previous_;
};

struct PathDetails {
    std::wstring friendly_name;
    std::uint32_t refresh_millihertz = 0;
};

/// GDI name → what only the CCD API knows about the monitor behind it.
std::map<std::wstring, PathDetails> path_details() {
    std::map<std::wstring, PathDetails> details;
    const auto paths = active_display_paths();
    if (!paths) {
        return details;
    }
    for (const DISPLAYCONFIG_PATH_INFO& path : *paths) {
        auto name = source_gdi_name(path);
        if (!name) {
            continue;
        }
        const DISPLAYCONFIG_RATIONAL rate = path.targetInfo.refreshRate;
        const std::uint32_t refresh =
            rate.Denominator == 0
                ? 0
                : static_cast<std::uint32_t>(
                      (static_cast<unsigned long long>(rate.Numerator) * 1000U) / rate.Denominator);
        details[*name] = {.friendly_name = target_friendly_name(path),
                          .refresh_millihertz = refresh};
    }
    return details;
}

BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC /*dc*/, LPRECT /*rect*/, LPARAM context) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr): EnumDisplayMonitors passes our pointer back
    reinterpret_cast<std::vector<HMONITOR>*>(context)->push_back(monitor);
    return TRUE;
}

std::optional<MonitorCandidate> describe(HMONITOR monitor,
                                         const std::map<std::wstring, PathDetails>& details) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) {
        return std::nullopt;
    }
    const std::wstring device(std::data(info.szDevice));
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &mode) == FALSE) {
        return std::nullopt;
    }
    MonitorCandidate candidate{
        .key = utf8(device),
        .rect = MonitorRect{.left = info.rcMonitor.left,
                            .top = info.rcMonitor.top,
                            .right = info.rcMonitor.right,
                            .bottom = info.rcMonitor.bottom},
        .width_px = mode.dmPelsWidth,
        .height_px = mode.dmPelsHeight,
        .refresh_millihertz = mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency * 1000U : 0U,
        .physical_width_mm = 0,
        .physical_height_mm = 0,
        .dpi = USER_DEFAULT_SCREEN_DPI,
        .name_utf8 = {},
    };
    if (const auto it = details.find(device); it != details.end()) {
        candidate.name_utf8 = utf8(it->second.friendly_name);
        if (it->second.refresh_millihertz != 0) {
            candidate.refresh_millihertz = it->second.refresh_millihertz; // 59.951, not 59
        }
    }
    if (HDC dc = CreateDCW(L"DISPLAY", device.c_str(), nullptr, nullptr); dc != nullptr) {
        candidate.physical_width_mm =
            static_cast<std::uint32_t>(std::max(GetDeviceCaps(dc, HORZSIZE), 0));
        candidate.physical_height_mm =
            static_cast<std::uint32_t>(std::max(GetDeviceCaps(dc, VERTSIZE), 0));
        DeleteDC(dc);
    }
    UINT dpi_x = 0;
    UINT dpi_y = 0;
    if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) && dpi_x != 0) {
        candidate.dpi = dpi_x;
    }
    return candidate;
}

} // namespace

std::vector<MonitorCandidate> active_physical_monitors() {
    const PhysicalPixelsScope scope;
    const auto vdd =
        vdd_gdi_device_name(); // fails when the driver is absent or idle: nothing to skip
    const auto details = path_details();
    std::vector<HMONITOR> handles;
    EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&handles));
    std::vector<MonitorCandidate> monitors;
    for (HMONITOR handle : handles) {
        auto candidate = describe(handle, details);
        if (!candidate) {
            continue;
        }
        if (vdd && candidate->key == utf8(vdd.value())) {
            continue;
        }
        monitors.push_back(std::move(*candidate));
    }
    return monitors;
}

} // namespace osplay::engine
