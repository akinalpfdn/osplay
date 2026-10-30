#include "android_watcher.hpp"
#include "engine_results.hpp"

#include <osplay/engine.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

using osplay::engine::guarded;
using osplay::engine::usb::AndroidUsbWatcher;

} // namespace

extern "C" {

osplay_engine_result
osplay_engine_android_watcher_start(const osplay_engine_android_callbacks* callbacks,
                                    osplay_engine_android_watcher** out_watcher) {
    return guarded([&] {
        if (out_watcher == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        *out_watcher = nullptr;
        if (callbacks == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const osplay_engine_android_callbacks shell = *callbacks;
        AndroidUsbWatcher::Callbacks forwarded;
        if (shell.on_attached != nullptr) {
            forwarded.attached = [shell](std::uint32_t id) {
                shell.on_attached(shell.context, id);
            };
        }
        if (shell.on_detached != nullptr) {
            forwarded.detached = [shell](std::uint32_t id) {
                shell.on_detached(shell.context, id);
            };
        }
        if (shell.on_debugging_on != nullptr) {
            forwarded.debugging_on = [shell] { shell.on_debugging_on(shell.context); };
        }
        if (shell.on_log != nullptr) {
            forwarded.log = [shell](osplay_log_level level, const std::string& message) {
                shell.on_log(shell.context, level, message.c_str());
            };
        }
        auto watcher = AndroidUsbWatcher::start(std::move(forwarded));
        if (!watcher) {
            return OSPLAY_ENGINE_ERROR_INTERNAL;
        }
        auto handle = std::make_unique<osplay_engine_android_watcher>();
        handle->watcher = std::move(watcher);
        *out_watcher = handle.release(); // ownership passes to the caller until _destroy
        return OSPLAY_ENGINE_OK;
    });
}

void osplay_engine_android_watcher_destroy(osplay_engine_android_watcher* watcher) {
    // Taking ownership back from the caller.
    const std::unique_ptr<osplay_engine_android_watcher> owned(watcher);
    if (owned) {
        // A host session may still hold the watcher for a moment; its callbacks end here.
        owned->watcher->stop();
    }
}

} // extern "C"
