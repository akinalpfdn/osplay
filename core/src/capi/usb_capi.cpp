#include "transport/usb_watcher.hpp"

#include <osplay/osplay.h>

#include <memory>

namespace {

class CallbackUsbObserver final : public osplay::transport::UsbObserver {
public:
    explicit CallbackUsbObserver(const osplay_usb_callbacks& callbacks) : callbacks_(callbacks) {}

    void on_attached(std::uint32_t device_id) override {
        callbacks_.on_attached(callbacks_.context, device_id);
    }
    void on_detached(std::uint32_t device_id) override {
        callbacks_.on_detached(callbacks_.context, device_id);
    }

private:
    osplay_usb_callbacks callbacks_;
};

} // namespace

/// The handle behind osplay_usb_watcher*; the watcher stops before the observer it calls goes.
struct osplay_usb_watcher {
    explicit osplay_usb_watcher(const osplay_usb_callbacks& callbacks)
        : observer(callbacks), watcher(observer) {}

    CallbackUsbObserver observer;
    osplay::transport::UsbWatcher watcher;
};

extern "C" {

osplay_result osplay_usb_watcher_create(const osplay_usb_callbacks* callbacks,
                                        osplay_usb_watcher** out_watcher) {
    if (out_watcher == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    *out_watcher = nullptr;
    if (callbacks == nullptr || callbacks->on_attached == nullptr ||
        callbacks->on_detached == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    try {
        *out_watcher = std::make_unique<osplay_usb_watcher>(*callbacks).release();
        return OSPLAY_OK;
    } catch (...) { // only allocation and thread creation can throw
        return OSPLAY_ERROR_INTERNAL;
    }
}

void osplay_usb_watcher_destroy(osplay_usb_watcher* watcher) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the C API hands out the raw pointer
    delete watcher;
}

} // extern "C"
