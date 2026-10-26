#pragma once

// Follows the iPads on a USB cable through usbmuxd ("Listen"), on a thread of its own. When
// usbmuxd is missing (Windows without Apple Devices) or restarts, it tries again every few seconds
// and reports the devices it knew as detached.

#include "transport/asio_session.hpp"
#include "util/time.hpp"

#include <cstdint>
#include <memory>

namespace osplay::transport {

class UsbObserver {
public:
    virtual ~UsbObserver() = default;
    /// Called on the watcher's thread; only devices on a cable, not those usbmuxd reaches by Wi-Fi.
    virtual void on_attached(std::uint32_t device_id) = 0;
    virtual void on_detached(std::uint32_t device_id) = 0;
};

class UsbWatcher {
public:
    explicit UsbWatcher(UsbObserver& observer, UsbmuxAddress address = {},
                        Microseconds retry_interval = Microseconds{3'000'000});
    /// Stops the thread; no callback runs after it returns. Must not be called from a callback.
    ~UsbWatcher();
    UsbWatcher(const UsbWatcher&) = delete;
    UsbWatcher& operator=(const UsbWatcher&) = delete;
    UsbWatcher(UsbWatcher&&) = delete;
    UsbWatcher& operator=(UsbWatcher&&) = delete;

    class Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace osplay::transport
