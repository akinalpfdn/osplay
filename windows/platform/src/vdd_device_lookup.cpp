#include <osplay/win/vdd_device_lookup.hpp>

#include <cwchar>
#include <utility>
#include <vector>

namespace osplay::win {
namespace {

bool has_hardware_id(HDEVINFO set, SP_DEVINFO_DATA& data, const wchar_t* wanted) {
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, nullptr, nullptr, 0, &required);
    if (required == 0) {
        return false;
    }
    std::vector<wchar_t> ids((required / sizeof(wchar_t)) + 1, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(
            set, &data, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids.data()),
            static_cast<DWORD>(ids.size() * sizeof(wchar_t)), nullptr)) {
        return false;
    }
    // REG_MULTI_SZ: a sequence of NUL-terminated strings ending with an empty string.
    for (const wchar_t* id = ids.data(); *id != L'\0'; id += std::wcslen(id) + 1) {
        if (_wcsicmp(id, wanted) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<FoundDevice> find_device(const wchar_t* hardware_id) {
    HDEVINFO raw_set =
        SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (raw_set == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    DeviceInfoSet set(raw_set);
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set.get(), index, &data); ++index) {
        if (has_hardware_id(set.get(), data, hardware_id)) {
            return FoundDevice{.set = std::move(set), .data = data};
        }
    }
    return std::nullopt;
}

std::optional<std::wstring> instance_id(const FoundDevice& device) {
    DWORD required = 0;
    SP_DEVINFO_DATA data = device.data;
    SetupDiGetDeviceInstanceIdW(device.set.get(), &data, nullptr, 0, &required);
    if (required == 0) {
        return std::nullopt;
    }
    std::vector<wchar_t> buffer(required, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(device.set.get(), &data, buffer.data(), required, nullptr)) {
        return std::nullopt;
    }
    return std::wstring(buffer.data());
}

} // namespace osplay::win
