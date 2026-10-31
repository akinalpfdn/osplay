#pragma once

// Which USB devices Osplay switches into Android's accessory mode on Windows (TAB-05), decided
// from the IDs Windows gives a device and its interfaces. No Windows calls, so it is tested on its
// own. The switch goes through UsbDk, which detaches a device from its driver while it sends the
// requests, so only devices Windows already knows as phones or tablets are touched.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace osplay::engine::usb {

struct UsbIds {
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;

    bool operator==(const UsbIds&) const = default;
};

/// The vendor and product of a USB device instance or hardware ID such as
/// "USB\VID_04E8&PID_6860\RFCW714678P", in any letter case.
[[nodiscard]] std::optional<UsbIds> parse_usb_ids(std::wstring_view device_id);

/// "USB\VID_04E8&PID_6860\RFCW714678P" → {"USB\VID_04E8&PID_6860", "RFCW714678P"}, the two
/// halves UsbDk names a device by.
struct InstanceParts {
    std::wstring device_id;
    std::wstring instance_id;
};
[[nodiscard]] std::optional<InstanceParts> split_instance_id(std::wstring_view instance_id);

enum class AccessoryKind : std::uint8_t {
    None,
    Accessory,        ///< 18D1:2D00, USB debugging off: the cable works
    AccessoryWithAdb, ///< 18D1:2D01, USB debugging on: Windows keeps the accessory on MTP
};

[[nodiscard]] AccessoryKind accessory_kind(UsbIds ids);

enum class CableAction : std::uint8_t {
    Leave,         ///< not an Android device in its normal mode
    Switch,        ///< send the switch to the accessory
    DebuggingIsOn, ///< an ADB interface: switched, it would come back as 18D1:2D01
};

/// What to do with a device plugged in, from the compatible IDs of the device and its interfaces.
/// Android devices show MTP or PTP in their normal mode; Apple's (iPads use usbmuxd) and devices
/// already in accessory mode are left alone. With USB debugging on the device also shows ADB, and
/// is not switched at all: Windows would keep the accessory on MTP, and the switch would interrupt
/// adb and ask the device's user to open Osplay for nothing.
[[nodiscard]] CableAction cable_action(UsbIds ids, std::span<const std::wstring> compatible_ids);

} // namespace osplay::engine::usb
