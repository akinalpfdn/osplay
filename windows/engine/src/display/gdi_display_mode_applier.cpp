#include "gdi_display_mode_applier.hpp"

#include "../engine_tunables.hpp"
#include "vdd_monitor.hpp"

#include <windows.h>

#include <cmath>
#include <cstddef>

namespace osplay::engine {
namespace {

using Result = win::Expected<std::vector<std::wstring>, DisplayError>;

DEVMODEW mode_for(const VirtualMonitorConfig& config) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    mode.dmPelsWidth = config.width;
    mode.dmPelsHeight = config.height;
    mode.dmDisplayFrequency = static_cast<DWORD>(std::lround(config.refresh_millihertz / 1000.0));
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    return mode;
}

} // namespace

Result GdiDisplayModeApplier::apply(std::span<const VirtualMonitorConfig> configs) {
    // Enabling the devices returns before Windows has added the monitors to the desktop, and they
    // arrive one by one.
    const ULONGLONG deadline = GetTickCount64() + kMonitorArrivalTimeoutMilliseconds;
    auto names = vdd_gdi_device_names();
    while ((!names ? names.error() == DisplayError::MonitorNotFound
                   : names.value().size() < configs.size()) &&
           GetTickCount64() < deadline) {
        Sleep(kMonitorArrivalPollMilliseconds);
        names = vdd_gdi_device_names();
    }
    if (!names) {
        return Result::failure(names.error());
    }
    if (names.value().size() < configs.size()) {
        return Result::failure(DisplayError::MonitorNotFound);
    }
    names.value().resize(configs.size());

    // Every mode is stored first and applied together, so Windows rearranges the desktop once.
    for (std::size_t i = 0; i < configs.size(); ++i) {
        DEVMODEW mode = mode_for(configs[i]);
        if (ChangeDisplaySettingsExW(names.value()[i].c_str(), &mode, nullptr,
                                     CDS_UPDATEREGISTRY | CDS_NORESET,
                                     nullptr) != DISP_CHANGE_SUCCESSFUL) {
            return Result::failure(DisplayError::ModeChangeFailed);
        }
    }
    if (ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr) != DISP_CHANGE_SUCCESSFUL) {
        return Result::failure(DisplayError::ModeChangeFailed);
    }

    for (std::size_t i = 0; i < configs.size(); ++i) {
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        if (!EnumDisplaySettingsExW(names.value()[i].c_str(), ENUM_CURRENT_SETTINGS, &current, 0) ||
            current.dmPelsWidth != configs[i].width || current.dmPelsHeight != configs[i].height) {
            return Result::failure(DisplayError::ModeChangeFailed);
        }
    }
    return names;
}

} // namespace osplay::engine
