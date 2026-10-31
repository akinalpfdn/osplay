#pragma once

// UsbDk (Red Hat, Apache-2.0), loaded at run time from System32, where Osplay's installer puts
// it: sends an Android device the AOA switch while Windows' own drivers (MTP, Samsung's composite
// driver) hold it, which WinUSB cannot (TAB-05). Only the switch goes through UsbDk; the accessory
// is read through WinUSB.

#include <osplay/osplay.h>

#include <windows.h>

#include <memory>
#include <string>

namespace osplay::engine::usb {

class UsbDk {
public:
    /// Null when UsbDk is not installed.
    [[nodiscard]] static std::unique_ptr<UsbDk> load();

    ~UsbDk();
    UsbDk(const UsbDk&) = delete;
    UsbDk& operator=(const UsbDk&) = delete;
    UsbDk(UsbDk&&) = delete;
    UsbDk& operator=(UsbDk&&) = delete;

    /// Switches the device UsbDk names `device_id` ("USB\VID_04E8&PID_6860") and `instance_id`
    /// (its serial) into Osplay's accessory: OSPLAY_OK, OSPLAY_ERROR_UNSUPPORTED_VERSION for a
    /// device without AOA, OSPLAY_ERROR_NETWORK when UsbDk cannot reach it, or
    /// OSPLAY_ERROR_TIMED_OUT for a request that did not finish.
    [[nodiscard]] osplay_result switch_to_accessory(const std::wstring& device_id,
                                                    const std::wstring& instance_id) const;

    struct Functions; // UsbDkHelper's exports

private:
    UsbDk(HMODULE module, std::unique_ptr<Functions> functions);

    HMODULE module_;
    std::unique_ptr<Functions> functions_;
};

} // namespace osplay::engine::usb
