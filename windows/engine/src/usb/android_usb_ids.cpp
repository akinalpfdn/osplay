#include "android_usb_ids.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cwctype>

namespace osplay::engine::usb {
namespace {

constexpr std::uint16_t kAppleVendorId = 0x05AC;

/// Compatible IDs an Android device's interfaces get in its normal mode.
constexpr std::array<std::wstring_view, 2> kAndroidInterfaceIds = {
    L"USB\\MS_COMP_MTP",                  // MTP through a Microsoft OS descriptor
    L"USB\\CLASS_06&SUBCLASS_01&PROT_01", // MTP or PTP (still image class)
};
/// ADB's interface, which a device shows only with USB debugging on.
constexpr std::wstring_view kAdbInterfaceId = L"USB\\CLASS_FF&SUBCLASS_42&PROT_01";

std::wstring upper(std::wstring_view text) {
    std::wstring result(text);
    std::ranges::transform(result, result.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return result;
}

std::optional<std::uint16_t> hex_after(std::wstring_view text, std::wstring_view marker) {
    const std::size_t at = text.find(marker);
    if (at == std::wstring_view::npos || at + marker.size() + 4 > text.size()) {
        return std::nullopt;
    }
    std::array<char, 4> digits{};
    for (std::size_t i = 0; i < digits.size(); ++i) {
        const wchar_t c = text[at + marker.size() + i];
        if (!std::iswxdigit(c)) {
            return std::nullopt;
        }
        digits.at(i) = static_cast<char>(c);
    }
    std::uint16_t value = 0;
    const auto [end, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<UsbIds> parse_usb_ids(std::wstring_view device_id) {
    const std::wstring id = upper(device_id);
    const auto vendor = hex_after(id, L"VID_");
    const auto product = hex_after(id, L"PID_");
    if (!vendor || !product) {
        return std::nullopt;
    }
    return UsbIds{.vendor_id = *vendor, .product_id = *product};
}

std::optional<InstanceParts> split_instance_id(std::wstring_view instance_id) {
    const std::size_t slash = instance_id.rfind(L'\\');
    if (slash == std::wstring_view::npos || slash == 0 || slash + 1 == instance_id.size()) {
        return std::nullopt;
    }
    return InstanceParts{.device_id = std::wstring(instance_id.substr(0, slash)),
                         .instance_id = std::wstring(instance_id.substr(slash + 1))};
}

AccessoryKind accessory_kind(UsbIds ids) {
    if (osplay_aoa_is_accessory(ids.vendor_id, ids.product_id) == 0) {
        return AccessoryKind::None;
    }
    // The core knows the accessory IDs; the one with ADB is the debugging variant.
    constexpr std::uint16_t kAccessoryWithAdb = 0x2D01;
    return ids.product_id == kAccessoryWithAdb ? AccessoryKind::AccessoryWithAdb
                                               : AccessoryKind::Accessory;
}

CableAction cable_action(UsbIds ids, std::span<const std::wstring> compatible_ids) {
    if (accessory_kind(ids) != AccessoryKind::None || ids.vendor_id == kAppleVendorId) {
        return CableAction::Leave;
    }
    bool android = false;
    for (const std::wstring& id : compatible_ids) {
        const std::wstring normalized = upper(id);
        if (normalized == kAdbInterfaceId) {
            return CableAction::DebuggingIsOn;
        }
        android =
            android || std::ranges::find(kAndroidInterfaceIds, std::wstring_view(normalized)) !=
                           kAndroidInterfaceIds.end();
    }
    return android ? CableAction::Switch : CableAction::Leave;
}

} // namespace osplay::engine::usb
