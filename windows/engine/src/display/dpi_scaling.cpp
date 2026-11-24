#include "dpi_scaling.hpp"

#include "display_config.hpp"
#include "dpi_scale.hpp"

#include <windows.h>

namespace osplay::engine {
namespace {

// Request types and layouts Settings › Display uses; not in the Windows SDK. The sizes are part of
// the contract: Windows rejects a request whose header size differs.
constexpr auto kGetDpiScale = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-3);
constexpr auto kSetDpiScale = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(-4);

struct DpiScaleGet {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    std::int32_t min_relative;
    std::int32_t current_relative;
    std::int32_t max_relative;
};
static_assert(sizeof(DpiScaleGet) == 0x20);

struct DpiScaleSet {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    std::int32_t relative;
};
static_assert(sizeof(DpiScaleSet) == 0x18);

} // namespace

win::Status<DisplayError> set_display_scale(const std::wstring& gdi_name, std::uint16_t percent) {
    const auto paths = active_display_paths();
    if (!paths) {
        return win::Status<DisplayError>::failure(DisplayError::MonitorNotFound);
    }
    for (const DISPLAYCONFIG_PATH_INFO& path : *paths) {
        if (source_gdi_name(path) != gdi_name) {
            continue;
        }
        // Scale belongs to the source, like the GDI name.
        DpiScaleGet range{.header = {.type = kGetDpiScale,
                                     .size = static_cast<UINT32>(sizeof(DpiScaleGet)),
                                     .adapterId = path.sourceInfo.adapterId,
                                     .id = path.sourceInfo.id},
                          .min_relative = 0,
                          .current_relative = 0,
                          .max_relative = 0};
        if (DisplayConfigGetDeviceInfo(&range.header) != ERROR_SUCCESS) {
            return win::Status<DisplayError>::failure(DisplayError::ModeChangeFailed);
        }
        const auto step = relative_scale_step(percent, {.min_relative = range.min_relative,
                                                        .current_relative = range.current_relative,
                                                        .max_relative = range.max_relative});
        if (!step) {
            return win::Status<DisplayError>::failure(DisplayError::ModeChangeFailed);
        }
        if (*step == range.current_relative) {
            return win::Status<DisplayError>::ok();
        }
        DpiScaleSet request{.header = {.type = kSetDpiScale,
                                       .size = static_cast<UINT32>(sizeof(DpiScaleSet)),
                                       .adapterId = path.sourceInfo.adapterId,
                                       .id = path.sourceInfo.id},
                            .relative = *step};
        if (DisplayConfigSetDeviceInfo(&request.header) != ERROR_SUCCESS) {
            return win::Status<DisplayError>::failure(DisplayError::ModeChangeFailed);
        }
        return win::Status<DisplayError>::ok();
    }
    return win::Status<DisplayError>::failure(DisplayError::MonitorNotFound);
}

} // namespace osplay::engine
