#include "transport/aoa.hpp"
#include "transport/usb_watcher.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <memory>
#include <span>

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

osplay_result osplay_aoa_switch(void* context, osplay_usb_control_transfer transfer) {
    if (transfer == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    namespace aoa = osplay::transport::aoa;
    const auto run = [&](aoa::ControlRequest& request, std::uint16_t& transferred) {
        return transfer(context, request.request_type, request.request, request.value,
                        request.index, request.data.data(),
                        static_cast<std::uint16_t>(request.data.size()), &transferred);
    };
    try {
        auto ask = aoa::protocol_request();
        std::uint16_t answered = 0;
        if (const auto result = run(ask, answered); result != OSPLAY_OK) {
            return result;
        }
        const auto version = aoa::parse_protocol(
            std::span(ask.data).first(std::min<std::size_t>(answered, ask.data.size())));
        if (!version) {
            return OSPLAY_ERROR_UNSUPPORTED_VERSION;
        }
        for (auto& request : aoa::start_requests()) {
            std::uint16_t sent = 0;
            if (const auto result = run(request, sent); result != OSPLAY_OK) {
                return result;
            }
        }
        return OSPLAY_OK;
    } catch (...) { // only allocation can throw
        return OSPLAY_ERROR_INTERNAL;
    }
}

uint8_t osplay_aoa_is_accessory(uint16_t vendor_id, uint16_t product_id) {
    return osplay::transport::aoa::is_accessory(vendor_id, product_id) ? 1 : 0;
}

} // extern "C"
