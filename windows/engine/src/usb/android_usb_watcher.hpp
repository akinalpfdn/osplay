#pragma once

// Android devices on the USB cable while a Windows host shares (TAB-05): every phone-like device
// plugged in, or already there, is switched to Osplay's accessory through UsbDk; each accessory
// that comes back on WinUSB is reported. A device with USB debugging on (an ADB interface, or an
// accessory as 18D1:2D01, which Windows keeps on MTP) is reported so the shell can ask to turn it
// off.

#include "usb/usbdk.hpp"

#include <osplay/osplay.h>

#include <windows.h>

#include <cfgmgr32.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace osplay::engine::usb {

class AndroidUsbWatcher {
public:
    /// Called on the watcher's own thread; they must return quickly.
    struct Callbacks {
        std::function<void(std::uint32_t accessory_id)> attached;
        std::function<void(std::uint32_t accessory_id)> detached;
        std::function<void()> debugging_on;
        std::function<void(osplay_log_level, const std::string&)> log;
    };

    /// Null when Windows refuses device notifications. Without UsbDk nothing is switched, but
    /// accessories already switched are still reported.
    [[nodiscard]] static std::unique_ptr<AndroidUsbWatcher> start(Callbacks callbacks);

    /// Stops watching: no callback runs after it returns. Other calls still work.
    void stop();

    ~AndroidUsbWatcher(); ///< stops
    AndroidUsbWatcher(const AndroidUsbWatcher&) = delete;
    AndroidUsbWatcher& operator=(const AndroidUsbWatcher&) = delete;
    AndroidUsbWatcher(AndroidUsbWatcher&&) = delete;
    AndroidUsbWatcher& operator=(AndroidUsbWatcher&&) = delete;

    /// The WinUSB interface path of a reported accessory, while it is attached.
    [[nodiscard]] std::optional<std::wstring> interface_path(std::uint32_t accessory_id) const;

    /// Osplay on the device opened the accessory: its switches start counting again, so the device
    /// is switched again however often the cable is plugged in. Only switches nobody answered
    /// count towards the limit, which keeps an ignored "Open Osplay?" prompt from coming back.
    void accessory_opened(std::uint32_t accessory_id);

private:
    struct Event {
        bool arrived = true;
        std::wstring link; // the USB device's interface path
        std::chrono::steady_clock::time_point due;
    };
    struct Accessory {
        std::uint32_t id = 0;
        std::wstring interface_path;
        std::wstring serial; // the device's, in both modes; empty when it has none
    };

    explicit AndroidUsbWatcher(Callbacks callbacks);

    static DWORD CALLBACK on_notification(HCMNOTIFICATION notification, PVOID context,
                                          CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA data,
                                          DWORD size);
    void post(bool arrived, std::wstring link, std::chrono::milliseconds delay);
    void run();
    void arrived(const std::wstring& link);
    void removed(const std::wstring& link);
    void debugging_is_on() const;
    void log(osplay_log_level level, const std::string& message) const;

    Callbacks callbacks_;
    std::unique_ptr<UsbDk> usbdk_;
    HCMNOTIFICATION notification_ = nullptr;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Event> events_;
    bool stopping_ = false;
    std::map<std::wstring, Accessory> accessories_; // by USB device interface path
    std::map<std::wstring, int> switches_;          // by serial (or instance ID without one)
    std::map<std::wstring, int> lookups_;           // WinUSB interface lookups, by interface path
    std::uint32_t next_id_ = 1;
    std::thread worker_;
};

} // namespace osplay::engine::usb
