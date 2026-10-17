#include "../display/gdi_display_mode_applier.hpp"
#include "../display/helper_virtual_display_provider.hpp"
#include "../display/pipe_helper_channel.hpp"

#include <osplay/engine.h>

#include <exception>
#include <memory>

struct osplay_virtual_display {
    std::unique_ptr<osplay::engine::VirtualDisplayProvider> provider;
};

namespace {

using osplay::engine::DisplayError;

osplay_engine_result to_result(DisplayError error) {
    switch (error) {
    case DisplayError::InvalidArgument:
        return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
    case DisplayError::HelperUnavailable:
        return OSPLAY_ENGINE_ERROR_HELPER_UNAVAILABLE;
    case DisplayError::HelperAccessDenied:
        return OSPLAY_ENGINE_ERROR_HELPER_ACCESS_DENIED;
    case DisplayError::HelperNotTrusted:
        return OSPLAY_ENGINE_ERROR_HELPER_NOT_TRUSTED;
    case DisplayError::ProtocolMismatch:
        return OSPLAY_ENGINE_ERROR_PROTOCOL_MISMATCH;
    case DisplayError::ConnectionLost:
        return OSPLAY_ENGINE_ERROR_CONNECTION_LOST;
    case DisplayError::VddNotInstalled:
        return OSPLAY_ENGINE_ERROR_VDD_NOT_INSTALLED;
    case DisplayError::DeviceControlFailed:
        return OSPLAY_ENGINE_ERROR_DEVICE_CONTROL_FAILED;
    case DisplayError::ConfigWriteFailed:
        return OSPLAY_ENGINE_ERROR_CONFIG_WRITE_FAILED;
    case DisplayError::Rejected:
        return OSPLAY_ENGINE_ERROR_REJECTED;
    case DisplayError::MonitorNotFound:
        return OSPLAY_ENGINE_ERROR_MONITOR_NOT_FOUND;
    case DisplayError::ModeChangeFailed:
        return OSPLAY_ENGINE_ERROR_MODE_CHANGE_FAILED;
    }
    return OSPLAY_ENGINE_ERROR_INTERNAL;
}

/// No C++ exception may cross the C boundary.
template <typename Body>
osplay_engine_result guarded(const Body& body) noexcept {
    try {
        return body();
    } catch (...) {
        return OSPLAY_ENGINE_ERROR_INTERNAL;
    }
}

} // namespace

extern "C" {

osplay_version osplay_engine_core_version() {
    return osplay_core_version();
}

osplay_engine_result osplay_virtual_display_open(osplay_virtual_display** out_display) {
    return guarded([&] {
        if (out_display == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        *out_display = nullptr;
        auto channel = osplay::engine::PipeHelperChannel::connect();
        if (!channel) {
            return to_result(channel.error());
        }
        auto display = std::make_unique<osplay_virtual_display>();
        display->provider = std::make_unique<osplay::engine::HelperVirtualDisplayProvider>(
            std::move(channel.value()), std::make_unique<osplay::engine::GdiDisplayModeApplier>());
        *out_display = display.release(); // ownership passes to the caller until _close
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result
osplay_virtual_display_create_monitor(osplay_virtual_display* display,
                                      const osplay_virtual_monitor_config* config) {
    return guarded([&] {
        if (display == nullptr || config == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const osplay::engine::VirtualMonitorConfig monitor{
            .width = config->width,
            .height = config->height,
            .refresh_millihertz = config->refresh_millihertz,
            .gpu_name = config->gpu_name_utf8 != nullptr ? config->gpu_name_utf8 : "",
        };
        const auto created = display->provider->create_monitor(monitor);
        return created ? OSPLAY_ENGINE_OK : to_result(created.error());
    });
}

osplay_engine_result osplay_virtual_display_remove_monitor(osplay_virtual_display* display) {
    return guarded([&] {
        if (display == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const auto removed = display->provider->remove_monitor();
        return removed ? OSPLAY_ENGINE_OK : to_result(removed.error());
    });
}

osplay_engine_result osplay_virtual_display_status(osplay_virtual_display* display,
                                                   osplay_virtual_monitor_status* out_status) {
    return guarded([&] {
        if (display == nullptr || out_status == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const auto status = display->provider->status();
        if (!status) {
            return to_result(status.error());
        }
        *out_status = osplay_virtual_monitor_status{
            .driver_installed = status.value().vdd_installed ? uint8_t{1} : uint8_t{0},
            .monitor_present = status.value().device_enabled ? uint8_t{1} : uint8_t{0},
            .configured = status.value().configured ? uint8_t{1} : uint8_t{0},
            .width = status.value().width,
            .height = status.value().height,
            .refresh_millihertz = status.value().refresh_millihertz,
        };
        return OSPLAY_ENGINE_OK;
    });
}

void osplay_virtual_display_close(osplay_virtual_display* display) {
    // Taking ownership back; closing the pipe also makes the helper remove the monitor.
    const std::unique_ptr<osplay_virtual_display> owned(display);
    if (owned) {
        static_cast<void>(guarded([&] {
            const auto removed = owned->provider->remove_monitor();
            return removed ? OSPLAY_ENGINE_OK : to_result(removed.error());
        }));
    }
}

} // extern "C"
