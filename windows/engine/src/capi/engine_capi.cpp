#include "../display/gdi_display_mode_applier.hpp"
#include "../display/pipe_helper_channel.hpp"
#include "../display/virtual_display_hub.hpp"
#include "engine_results.hpp"

#include <osplay/engine.h>

#include <exception>
#include <memory>
#include <span>
#include <vector>

/// Its own helper connection: the developer tool and the shell's health check run apart from any
/// host session.
struct osplay_virtual_display {
    std::unique_ptr<osplay::engine::VirtualDisplayProvider> provider;
};

namespace {

using osplay::engine::guarded;
using osplay::engine::to_result;

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
        display->provider = std::make_unique<osplay::engine::HubVirtualDisplays>(
            std::make_shared<osplay::engine::VirtualDisplayHub>(
                std::move(channel.value()),
                std::make_unique<osplay::engine::GdiDisplayModeApplier>()));
        *out_display = display.release(); // ownership passes to the caller until _close
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result
osplay_virtual_display_create_monitors(osplay_virtual_display* display,
                                       const osplay_virtual_monitor_config* configs, size_t count) {
    return guarded([&] {
        if (display == nullptr || configs == nullptr || count == 0) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        // More than the helper accepts is rejected by the provider.
        std::vector<osplay::engine::VirtualMonitorConfig> monitors;
        for (const osplay_virtual_monitor_config& config : std::span(configs, count)) {
            monitors.push_back({
                .width = config.width,
                .height = config.height,
                .refresh_millihertz = config.refresh_millihertz,
                .gpu_name = config.gpu_name_utf8 != nullptr ? config.gpu_name_utf8 : "",
            });
        }
        const auto created = display->provider->create_monitors(monitors);
        return created ? OSPLAY_ENGINE_OK : to_result(created.error());
    });
}

osplay_engine_result osplay_virtual_display_remove_monitor(osplay_virtual_display* display) {
    return guarded([&] {
        if (display == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const auto removed = display->provider->remove_monitors();
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
            .monitor_present = status.value().enabled_devices != 0 ? uint8_t{1} : uint8_t{0},
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
            const auto removed = owned->provider->remove_monitors();
            return removed ? OSPLAY_ENGINE_OK : to_result(removed.error());
        }));
    }
}

} // extern "C"
