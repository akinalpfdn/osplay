#include <osplay/win/vdd_device_lookup.hpp>

#include <algorithm>
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

std::optional<std::wstring> instance_id(HDEVINFO set, SP_DEVINFO_DATA& data) {
    DWORD required = 0;
    SetupDiGetDeviceInstanceIdW(set, &data, nullptr, 0, &required);
    if (required == 0) {
        return std::nullopt;
    }
    std::vector<wchar_t> buffer(required, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(set, &data, buffer.data(), required, nullptr)) {
        return std::nullopt;
    }
    return std::wstring(buffer.data());
}

} // namespace

std::vector<std::wstring> device_instances(const wchar_t* hardware_id) {
    HDEVINFO raw_set =
        SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (raw_set == INVALID_HANDLE_VALUE) {
        return {};
    }
    const DeviceInfoSet set(raw_set);
    std::vector<std::wstring> instances;
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set.get(), index, &data); ++index) {
        if (has_hardware_id(set.get(), data, hardware_id)) {
            if (auto id = instance_id(set.get(), data)) {
                instances.push_back(std::move(*id));
            }
        }
    }
    std::ranges::sort(instances);
    return instances;
}

std::optional<FoundDevice> open_device(const std::wstring& instance_id) {
    HDEVINFO raw_set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (raw_set == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    DeviceInfoSet set(raw_set);
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    if (!SetupDiOpenDeviceInfoW(set.get(), instance_id.c_str(), nullptr, 0, &data)) {
        return std::nullopt;
    }
    return FoundDevice{.set = std::move(set), .data = data};
}

} // namespace osplay::win
