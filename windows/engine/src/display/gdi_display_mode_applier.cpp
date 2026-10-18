#include "gdi_display_mode_applier.hpp"

#include "../engine_tunables.hpp"
#include "vdd_monitor.hpp"

#include <windows.h>

#include <cmath>

namespace osplay::engine {
namespace {

using Result = win::Status<DisplayError>;

} // namespace

win::Status<DisplayError> GdiDisplayModeApplier::apply(const VirtualMonitorConfig& config) {
    // Enabling the device returns before Windows has added the monitor to the desktop.
    const ULONGLONG deadline = GetTickCount64() + kMonitorArrivalTimeoutMilliseconds;
    auto gdi_name = vdd_gdi_device_name();
    while (!gdi_name && gdi_name.error() == DisplayError::MonitorNotFound &&
           GetTickCount64() < deadline) {
        Sleep(kMonitorArrivalPollMilliseconds);
        gdi_name = vdd_gdi_device_name();
    }
    if (!gdi_name) {
        return Result::failure(gdi_name.error());
    }

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    mode.dmPelsWidth = config.width;
    mode.dmPelsHeight = config.height;
    mode.dmDisplayFrequency = static_cast<DWORD>(std::lround(config.refresh_millihertz / 1000.0));
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    if (ChangeDisplaySettingsExW(gdi_name.value().c_str(), &mode, nullptr, CDS_UPDATEREGISTRY,
                                 nullptr) != DISP_CHANGE_SUCCESSFUL) {
        return Result::failure(DisplayError::ModeChangeFailed);
    }

    DEVMODEW current{};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsExW(gdi_name.value().c_str(), ENUM_CURRENT_SETTINGS, &current, 0) ||
        current.dmPelsWidth != config.width || current.dmPelsHeight != config.height) {
        return Result::failure(DisplayError::ModeChangeFailed);
    }
    return Result::ok();
}

} // namespace osplay::engine
