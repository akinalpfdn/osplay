#include "vdd_device.hpp"

#include "helper_constants.hpp"

#include <cfgmgr32.h>
#include <setupapi.h>
#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace osplay::helper {
namespace {

struct DeviceInfoSetDeleter {
    void operator()(HDEVINFO set) const noexcept { SetupDiDestroyDeviceInfoList(set); }
};
using DeviceInfoSet = std::unique_ptr<std::remove_pointer_t<HDEVINFO>, DeviceInfoSetDeleter>;

struct FoundDevice {
    DeviceInfoSet set;
    SP_DEVINFO_DATA data{};
};

bool has_hardware_id(HDEVINFO set, SP_DEVINFO_DATA& data, const wchar_t* wanted) {
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, SPDRP_HARDWAREID, nullptr, nullptr, 0, &required);
    if (required == 0) {
        return false;
    }
    std::vector<wchar_t> ids(required / sizeof(wchar_t) + 1, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(
            set, &data, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids.data()),
            static_cast<DWORD>(ids.size() * sizeof(wchar_t)), nullptr)) {
        return false;
    }
    // REG_MULTI_SZ: a sequence of NUL-terminated strings ending with an empty string.
    for (const wchar_t* id = ids.data(); *id != L'\0'; id += wcslen(id) + 1) {
        if (_wcsicmp(id, wanted) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<FoundDevice> find_vdd() {
    DeviceInfoSet set(
        SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT));
    if (set.get() == INVALID_HANDLE_VALUE) {
        set.release();
        return std::nullopt;
    }
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set.get(), index, &data); ++index) {
        if (has_hardware_id(set.get(), data, kVddHardwareId)) {
            return FoundDevice{.set = std::move(set), .data = data};
        }
    }
    return std::nullopt;
}

} // namespace

bool VddDevice::is_installed() {
    return find_vdd().has_value();
}

bool VddDevice::is_enabled() {
    const auto device = find_vdd();
    if (!device) {
        return false;
    }
    ULONG status = 0;
    ULONG problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, device->data.DevInst, 0) != CR_SUCCESS) {
        return false;
    }
    const bool disabled = (status & DN_HAS_PROBLEM) != 0 && problem == CM_PROB_DISABLED;
    return !disabled;
}

bool VddDevice::set_enabled(bool enabled) {
    auto device = find_vdd();
    if (!device) {
        log_.error("Virtual Display Driver device not found");
        return false;
    }

    SP_PROPCHANGE_PARAMS params{};
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    params.StateChange = enabled ? DICS_ENABLE : DICS_DISABLE;
    params.Scope = DICS_FLAG_GLOBAL;
    params.HwProfile = 0;

    if (!SetupDiSetClassInstallParamsW(device->set.get(), &device->data, &params.ClassInstallHeader,
                                       sizeof(params)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, device->set.get(), &device->data)) {
        log_.error("SetupAPI property change failed, error " + std::to_string(GetLastError()));
        return false;
    }
    return true;
}

} // namespace osplay::helper
