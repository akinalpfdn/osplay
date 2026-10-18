#include "../present/presentation_thread.hpp"
#include "../viewer/monitor_layout.hpp"
#include "../viewer/viewer_orchestrator.hpp"
#include "../viewer/windows_monitors.hpp"
#include "../viewer/windows_viewer_stream.hpp"
#include "engine_results.hpp"
#include "system_random.hpp"

#include <osplay/engine.h>
#include <osplay/osplay.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace {

using osplay::engine::guarded;
using osplay::engine::OfferedMonitor;
using osplay::engine::system_random;
using osplay::engine::to_result;

/// Streams the Windows viewer shows at once (DEVPLAN: one per physical monitor, up to two).
constexpr std::uint8_t kMaxViewerStreams = 2;

/// The orchestrator's view of the core session; detached before the session is destroyed.
class CapiViewerPort final : public osplay::engine::ViewerCorePort {
public:
    void attach(osplay_session* session) {
        const std::scoped_lock lock(mutex_);
        session_ = session;
    }
    osplay_session* detach() {
        const std::scoped_lock lock(mutex_);
        return std::exchange(session_, nullptr);
    }
    void close() {
        const std::scoped_lock lock(mutex_);
        if (session_ != nullptr) {
            static_cast<void>(osplay_session_close(session_)); // fails only when already closing
        }
    }
    [[nodiscard]] osplay_result confirm_pairing(std::uint8_t accepted) {
        const std::scoped_lock lock(mutex_);
        return session_ == nullptr ? OSPLAY_ERROR_INVALID_STATE
                                   : osplay_session_confirm_pairing(session_, accepted);
    }

    osplay_result request_keyframe(std::uint8_t stream_id) override {
        return call(osplay_session_request_keyframe, stream_id);
    }
    osplay_result pause_stream(std::uint8_t stream_id) override {
        return call(osplay_session_pause_stream, stream_id);
    }
    osplay_result resume_stream(std::uint8_t stream_id) override {
        return call(osplay_session_resume_stream, stream_id);
    }
    osplay_result stop_stream(std::uint8_t stream_id) override {
        return call(osplay_session_stop_stream, stream_id);
    }

private:
    osplay_result call(osplay_result (*function)(osplay_session*, std::uint8_t),
                       std::uint8_t stream_id) {
        const std::scoped_lock lock(mutex_);
        return session_ == nullptr ? OSPLAY_ERROR_INVALID_STATE : function(session_, stream_id);
    }

    std::mutex mutex_;
    osplay_session* session_ = nullptr;
};

std::vector<OfferedMonitor> offered_monitors() {
    return osplay::engine::offer_monitors(osplay::engine::active_physical_monitors());
}

} // namespace

struct osplay_engine_viewer final : private osplay::engine::ViewerObserver {
    explicit osplay_engine_viewer(const osplay_engine_viewer_callbacks& shell_callbacks)
        : callbacks(shell_callbacks) {}
    ~osplay_engine_viewer() override = default;
    osplay_engine_viewer(const osplay_engine_viewer&) = delete;
    osplay_engine_viewer& operator=(const osplay_engine_viewer&) = delete;
    osplay_engine_viewer(osplay_engine_viewer&&) = delete;
    osplay_engine_viewer& operator=(osplay_engine_viewer&&) = delete;

    osplay_engine_viewer_callbacks callbacks;
    std::vector<OfferedMonitor> offered;
    std::unique_ptr<osplay::engine::PresentationThread> presentation;
    std::unique_ptr<osplay::engine::WindowsViewerStreamFactory> streams;
    CapiViewerPort port;
    std::unique_ptr<osplay::engine::ViewerOrchestrator> orchestrator;
    osplay_session* session = nullptr;

    /// Guards `active`: the UI thread reaches the orchestrator only while it exists.
    std::mutex active_mutex;
    osplay::engine::ViewerOrchestrator* active = nullptr;

    template <typename Action>
    void with_orchestrator(const Action& action) {
        const std::scoped_lock lock(active_mutex);
        if (active != nullptr) {
            action(*active);
        }
    }

    osplay::engine::ViewerObserver& observer() { return *this; }

private:
    void on_visibility(bool visible) override {
        presentation->set_display_required(visible);
        if (callbacks.on_visibility != nullptr) {
            callbacks.on_visibility(callbacks.context, visible ? 1 : 0);
        }
    }
    void on_stream_failed(std::uint8_t stream_id, osplay::engine::PipelineError error) override {
        if (callbacks.on_stream_failed != nullptr) {
            callbacks.on_stream_failed(callbacks.context, stream_id, to_result(error));
        }
    }
};

namespace {

osplay_engine_viewer* viewer_of(void* context) {
    return static_cast<osplay_engine_viewer*>(context);
}

osplay_session_callbacks core_callbacks(osplay_engine_viewer* viewer) {
    osplay_session_callbacks callbacks{};
    callbacks.context = viewer;
    callbacks.on_state = [](void* context, osplay_session_state state, osplay_result reason) {
        auto* self = viewer_of(context);
        if (state == OSPLAY_SESSION_CLOSED) {
            self->orchestrator->on_session_closed();
        }
        self->callbacks.on_state(self->callbacks.context, state, reason);
    };
    callbacks.on_trust_query = [](void* context, const uint8_t* key) {
        auto* self = viewer_of(context);
        return self->callbacks.on_trust_query(self->callbacks.context, key);
    };
    callbacks.on_pairing_code = [](void* context, uint32_t code, const uint8_t* key) {
        auto* self = viewer_of(context);
        if (self->callbacks.on_pairing_code != nullptr) {
            self->callbacks.on_pairing_code(self->callbacks.context, code, key);
        }
    };
    callbacks.on_paired = [](void* context, const uint8_t* key) {
        auto* self = viewer_of(context);
        if (self->callbacks.on_paired != nullptr) {
            self->callbacks.on_paired(self->callbacks.context, key);
        }
    };
    callbacks.on_stream_config = [](void* context, const osplay_stream_config* config) {
        viewer_of(context)->orchestrator->on_stream_config(*config);
    };
    callbacks.on_access_unit = [](void* context, const osplay_access_unit* unit) {
        viewer_of(context)->orchestrator->on_access_unit(*unit);
    };
    callbacks.on_stream_stopped = [](void* context, uint8_t stream_id) {
        viewer_of(context)->orchestrator->on_stream_stopped(stream_id);
    };
    callbacks.on_stats = [](void* context, const osplay_session_stats* stats) {
        auto* self = viewer_of(context);
        if (self->callbacks.on_stats != nullptr) {
            self->callbacks.on_stats(self->callbacks.context, stats);
        }
    };
    callbacks.on_log = [](void* context, osplay_log_level level, const char* message) {
        auto* self = viewer_of(context);
        if (self->callbacks.on_log != nullptr) {
            self->callbacks.on_log(self->callbacks.context, level, message);
        }
    };
    return callbacks;
}

/// The requested monitors that are attached, or all of them when none were named.
std::vector<OfferedMonitor> select_monitors(const osplay_engine_viewer_config& config) {
    std::vector<OfferedMonitor> all = offered_monitors();
    if (config.monitor_ids == nullptr) {
        return all;
    }
    const std::span requested(config.monitor_ids, config.monitor_id_count);
    std::erase_if(all, [&](const OfferedMonitor& monitor) {
        return std::ranges::find(requested, monitor.monitor.id) == requested.end();
    });
    return all;
}

} // namespace

extern "C" {

osplay_engine_result osplay_engine_list_monitors(osplay_engine_monitor* out_monitors,
                                                 size_t capacity, size_t* out_count) {
    return guarded([&] {
        if (out_monitors == nullptr || out_count == nullptr || capacity < OSPLAY_MAX_MONITORS) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const auto monitors = offered_monitors();
        const std::span out(out_monitors, capacity);
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            const OfferedMonitor& source = monitors[i];
            const osplay_monitor& d = source.monitor.description;
            osplay_engine_monitor& target = out[i];
            target = osplay_engine_monitor{};
            target.monitor_id = d.monitor_id;
            target.width_px = d.width_px;
            target.height_px = d.height_px;
            target.refresh_millihertz = d.refresh_millihertz;
            target.physical_width_mm = d.physical_width_mm;
            target.physical_height_mm = d.physical_height_mm;
            target.scale_percent = d.scale_percent;
            target.left = source.monitor.rect.left;
            target.top = source.monitor.rect.top;
            target.right = source.monitor.rect.right;
            target.bottom = source.monitor.rect.bottom;
            // At most 64 bytes, so the NUL always fits.
            std::memcpy(static_cast<char*>(target.name_utf8), source.name.data(),
                        std::min(source.name.size(), sizeof(target.name_utf8) - 1));
        }
        *out_count = monitors.size();
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result osplay_engine_viewer_start(const osplay_engine_viewer_config* config,
                                                const osplay_engine_viewer_callbacks* callbacks,
                                                osplay_engine_viewer** out_viewer) {
    return guarded([&] {
        if (out_viewer == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        *out_viewer = nullptr;
        if (config == nullptr || config->secret_key == nullptr ||
            config->device_name_utf8 == nullptr || config->host_utf8 == nullptr ||
            config->port == 0 ||
            (config->monitor_ids == nullptr && config->monitor_id_count != 0) ||
            callbacks == nullptr || callbacks->on_state == nullptr ||
            callbacks->on_trust_query == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        auto viewer = std::make_unique<osplay_engine_viewer>(*callbacks);
        viewer->offered = select_monitors(*config);
        if (viewer->offered.empty()) {
            return OSPLAY_ENGINE_ERROR_MONITOR_NOT_FOUND;
        }
        osplay_engine_viewer* self = viewer.get();
        viewer->presentation = std::make_unique<osplay::engine::PresentationThread>(
            osplay::engine::PresentationThread::Callbacks{
                .on_hotkey =
                    [self] {
                        self->with_orchestrator(
                            [](osplay::engine::ViewerOrchestrator& o) { o.toggle_visibility(); });
                    },
                .on_display_change =
                    [self] {
                        const auto attached = osplay::engine::still_attached(
                            self->offered, osplay::engine::active_physical_monitors());
                        self->with_orchestrator([&](osplay::engine::ViewerOrchestrator& o) {
                            o.on_monitors_changed(attached);
                        });
                    },
            });
        viewer->streams =
            std::make_unique<osplay::engine::WindowsViewerStreamFactory>(*viewer->presentation);
        std::vector<osplay::engine::ViewerMonitor> monitors;
        std::vector<osplay_monitor> descriptions;
        for (const OfferedMonitor& monitor : viewer->offered) {
            monitors.push_back(monitor.monitor);
            osplay_monitor description = monitor.monitor.description;
            description.name = monitor.name.c_str();
            descriptions.push_back(description);
        }
        viewer->orchestrator = std::make_unique<osplay::engine::ViewerOrchestrator>(
            viewer->port, *viewer->streams, viewer->observer(), std::move(monitors));
        {
            const std::scoped_lock lock(viewer->active_mutex);
            viewer->active = viewer->orchestrator.get();
        }
        viewer->presentation->set_display_required(true);

        const osplay_session_config session_config{
            .platform = OSPLAY_PLATFORM_WINDOWS,
            .device_name = config->device_name_utf8,
            .capabilities = {.codecs = OSPLAY_CODEC_H264_ANNEX_B,
                             .max_streams = static_cast<std::uint8_t>(
                                 std::min<std::size_t>(descriptions.size(), kMaxViewerStreams)),
                             .flags = 0,
                             .max_width = 7680,
                             .max_height = 4320,
                             .max_refresh_hz = 240},
            .monitors = descriptions.data(),
            .monitor_count = descriptions.size(),
        };
        const osplay_random_source random{.fill = system_random, .context = nullptr};
        const osplay_session_callbacks session_callbacks = core_callbacks(viewer.get());
        osplay_session* session = nullptr;
        const osplay_result created = osplay_viewer_session_create(
            &session_config, config->secret_key, &random, &session_callbacks, config->host_utf8,
            config->port, &session);
        if (created != OSPLAY_OK) {
            osplay_engine_viewer_destroy(viewer.release());
            return created == OSPLAY_ERROR_INVALID_ARGUMENT ? OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT
                                                            : OSPLAY_ENGINE_ERROR_SESSION_FAILED;
        }
        viewer->session = session;
        viewer->port.attach(session);
        *out_viewer = viewer.release(); // ownership passes to the caller until _destroy
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result osplay_engine_viewer_hotkey_registered(osplay_engine_viewer* viewer,
                                                            uint8_t* out_registered) {
    return guarded([&] {
        if (viewer == nullptr || out_registered == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        *out_registered = viewer->presentation->hotkey_registered() ? 1 : 0;
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result osplay_engine_viewer_confirm_pairing(osplay_engine_viewer* viewer,
                                                          uint8_t accepted) {
    return guarded([&] {
        if (viewer == nullptr || accepted > 1) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        return viewer->port.confirm_pairing(accepted) == OSPLAY_OK
                   ? OSPLAY_ENGINE_OK
                   : OSPLAY_ENGINE_ERROR_SESSION_FAILED;
    });
}

osplay_engine_result osplay_engine_viewer_set_visible(osplay_engine_viewer* viewer,
                                                      uint8_t visible) {
    return guarded([&] {
        if (viewer == nullptr || visible > 1) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        viewer->orchestrator->set_visible(visible == 1);
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result osplay_engine_viewer_stream_stats(osplay_engine_viewer* viewer,
                                                       osplay_engine_stream_figures* out_stats,
                                                       size_t capacity, size_t* out_count) {
    return guarded([&] {
        if (viewer == nullptr || out_stats == nullptr || out_count == nullptr ||
            capacity < OSPLAY_MAX_STREAMS) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const std::span out(out_stats, capacity);
        std::size_t count = 0;
        for (const auto& [stream_id, stats] : viewer->orchestrator->stats()) {
            if (count == out.size()) {
                break;
            }
            out[count++] = {.stream_id = stream_id,
                            .frames_presented = stats.frames_presented,
                            .decode_errors = stats.decode_errors};
        }
        *out_count = count;
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result osplay_engine_viewer_stop(osplay_engine_viewer* viewer) {
    return guarded([&] {
        if (viewer == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        viewer->port.close();
        return OSPLAY_ENGINE_OK;
    });
}

void osplay_engine_viewer_destroy(osplay_engine_viewer* viewer) {
    // Taking ownership back from the caller.
    const std::unique_ptr<osplay_engine_viewer> owned(viewer);
    if (!owned) {
        return;
    }
    static_cast<void>(guarded([&] {
        // Order matters: no core callbacks into a dead orchestrator, no UI events into it either,
        // and the windows close on the UI thread before it stops.
        osplay_session_destroy(owned->port.detach()); // joins the core's I/O thread
        {
            const std::scoped_lock lock(owned->active_mutex);
            owned->active = nullptr;
        }
        owned->orchestrator.reset(); // closes every stream and its window
        owned->streams.reset();
        owned->presentation.reset();
        return OSPLAY_ENGINE_OK;
    }));
}

} // extern "C"
