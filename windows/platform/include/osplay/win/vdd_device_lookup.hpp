#pragma once

// Windows-only: locates the Virtual Display Driver's devices. Used by the helper (to add, enable
// and disable them) and by the engine (to find the displays they create).

#include <windows.h>

#include <setupapi.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace osplay::win {

/// Hardware ID the driver's root-enumerated devices are installed with (MttVDD.inf).
inline constexpr const wchar_t* kVddHardwareId = L"Root\\MttVDD";

struct DeviceInfoSetDeleter {
    void operator()(HDEVINFO set) const noexcept { SetupDiDestroyDeviceInfoList(set); }
};
using DeviceInfoSet = std::unique_ptr<std::remove_pointer_t<HDEVINFO>, DeviceInfoSetDeleter>;

struct FoundDevice {
    DeviceInfoSet set;
    SP_DEVINFO_DATA data{};
};

/// Instance IDs (e.g. ROOT\DISPLAY\0000) of the present (enabled or disabled) devices with the
/// given hardware ID, sorted, so the order stays the same while devices are only added.
[[nodiscard]] std::vector<std::wstring> device_instances(const wchar_t* hardware_id);

/// The present device with this instance ID, if any.
[[nodiscard]] std::optional<FoundDevice> open_device(const std::wstring& instance_id);

} // namespace osplay::win
