#pragma once

// Windows-only: locates the Virtual Display Driver's device. Used by the helper (to enable and
// disable it) and by the engine (to find the display it creates).

#include <windows.h>

#include <setupapi.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace osplay::win {

/// Hardware ID the driver's root-enumerated device is installed with (MttVDD.inf).
inline constexpr const wchar_t* kVddHardwareId = L"Root\\MttVDD";

struct DeviceInfoSetDeleter {
    void operator()(HDEVINFO set) const noexcept { SetupDiDestroyDeviceInfoList(set); }
};
using DeviceInfoSet = std::unique_ptr<std::remove_pointer_t<HDEVINFO>, DeviceInfoSetDeleter>;

struct FoundDevice {
    DeviceInfoSet set;
    SP_DEVINFO_DATA data{};
};

/// The present (enabled or disabled) device with the given hardware ID, if any.
[[nodiscard]] std::optional<FoundDevice> find_device(const wchar_t* hardware_id);

/// Device instance ID, e.g. ROOT\DISPLAY\0000.
[[nodiscard]] std::optional<std::wstring> instance_id(const FoundDevice& device);

} // namespace osplay::win
