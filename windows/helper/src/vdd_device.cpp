#include "vdd_device.hpp"

#include "helper_constants.hpp"

#include <osplay/win/vdd_device_lookup.hpp>

#include <windows.h>

#include <cfgmgr32.h>
#include <newdev.h>

#include <optional>
#include <string>
#include <vector>

namespace osplay::helper {
namespace {

std::vector<std::wstring> vdd_instances() {
    return win::device_instances(win::kVddHardwareId);
}

/// The display adapter device class (GUID_DEVCLASS_DISPLAY in devguid.h).
constexpr GUID kDisplayClass{.Data1 = 0x4d36e968,
                             .Data2 = 0xe325,
                             .Data3 = 0x11ce,
                             .Data4 = {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

} // namespace

bool VddDevices::is_installed() {
    return !vdd_instances().empty();
}

std::size_t VddDevices::count() {
    return vdd_instances().size();
}

bool VddDevices::add() {
    HDEVINFO raw_set = SetupDiCreateDeviceInfoList(&kDisplayClass, nullptr);
    if (raw_set == INVALID_HANDLE_VALUE) {
        log_.error("SetupAPI device list failed, error " + std::to_string(GetLastError()));
        return false;
    }
    const win::DeviceInfoSet set(raw_set);
    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    // A root device like the driver's installer makes: ROOT\DISPLAY\nnnn with the driver's ID.
    if (!SetupDiCreateDeviceInfoW(set.get(), L"DISPLAY", &kDisplayClass, nullptr, nullptr,
                                  DICD_GENERATE_ID, &data)) {
        log_.error("SetupAPI device creation failed, error " + std::to_string(GetLastError()));
        return false;
    }
    // REG_MULTI_SZ: the ID, its terminator and the list's terminator.
    std::wstring hardware_ids(win::kVddHardwareId);
    hardware_ids.append(2, L'\0');
    if (!SetupDiSetDeviceRegistryPropertyW(
            set.get(), &data, SPDRP_HARDWAREID, reinterpret_cast<const BYTE*>(hardware_ids.data()),
            static_cast<DWORD>(hardware_ids.size() * sizeof(wchar_t))) ||
        !SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set.get(), &data)) {
        log_.error("SetupAPI device registration failed, error " + std::to_string(GetLastError()));
        return false;
    }
    BOOL reboot = FALSE;
    if (!DiInstallDevice(nullptr, set.get(), &data, nullptr, DIIDFLAG_NOFINISHINSTALLUI, &reboot)) {
        const DWORD error = GetLastError();
        // A registered device without its driver would stay behind as an unknown device.
        static_cast<void>(SetupDiCallClassInstaller(DIF_REMOVE, set.get(), &data));
        log_.error("Installing the driver on a new device failed, error " + std::to_string(error));
        return false;
    }
    if (reboot != FALSE) {
        log_.warning("Windows asks for a reboot to finish adding a virtual display device");
    }
    return true;
}

bool VddDevices::is_enabled(std::size_t index) {
    const auto instances = vdd_instances();
    if (index >= instances.size()) {
        return false;
    }
    const auto device = win::open_device(instances[index]);
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

bool VddDevices::set_enabled(std::size_t index, bool enabled) {
    const auto instances = vdd_instances();
    if (index >= instances.size()) {
        log_.error("Virtual Display Driver device " + std::to_string(index) + " not found");
        return false;
    }
    for (int attempt = 1; attempt <= kDeviceChangeAttempts; ++attempt) {
        if (try_set_enabled(instances[index], enabled)) {
            return true;
        }
        if (attempt < kDeviceChangeAttempts) {
            Sleep(kDeviceChangeRetryDelayMilliseconds);
        }
    }
    return false;
}

bool VddDevices::try_set_enabled(const std::wstring& instance, bool enabled) {
    auto device = win::open_device(instance);
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
