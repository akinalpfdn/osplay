#include "display_config.hpp"

#include "../engine_tunables.hpp"

#include <iterator>

namespace osplay::engine {

std::optional<std::vector<DISPLAYCONFIG_PATH_INFO>> active_display_paths() {
    for (int attempt = 0; attempt < kDisplayConfigQueryAttempts; ++attempt) {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
            ERROR_SUCCESS) {
            return std::nullopt;
        }
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        const LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                                               &mode_count, modes.data(), nullptr);
        if (result == ERROR_SUCCESS) {
            paths.resize(path_count);
            return paths;
        }
        if (result != ERROR_INSUFFICIENT_BUFFER) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::wstring> source_gdi_name(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{
        .header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
                   .size = static_cast<UINT32>(sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME)),
                   .adapterId = path.sourceInfo.adapterId,
                   .id = path.sourceInfo.id},
        .viewGdiDeviceName = {},
    };
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return std::wstring(std::data(source.viewGdiDeviceName));
}

std::wstring target_friendly_name(const DISPLAYCONFIG_PATH_INFO& path) {
    DISPLAYCONFIG_TARGET_DEVICE_NAME target{
        .header = {.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,
                   .size = static_cast<UINT32>(sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME)),
                   .adapterId = path.targetInfo.adapterId,
                   .id = path.targetInfo.id},
        .flags = {},
        .outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER,
        .edidManufactureId = 0,
        .edidProductCodeId = 0,
        .connectorInstance = 0,
        .monitorFriendlyDeviceName = {},
        .monitorDevicePath = {},
    };
    if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
        return {};
    }
    return {std::data(target.monitorFriendlyDeviceName)};
}

} // namespace osplay::engine
