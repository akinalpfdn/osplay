#include "vdd_device.hpp"

#include "helper_constants.hpp"

#include <osplay/win/vdd_device_lookup.hpp>

#include <windows.h>

#include <cfgmgr32.h>

#include <optional>
#include <string>

namespace osplay::helper {
namespace {

std::optional<win::FoundDevice> find_vdd() {
    return win::find_device(win::kVddHardwareId);
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
    for (int attempt = 1; attempt <= kDeviceChangeAttempts; ++attempt) {
        if (try_set_enabled(enabled)) {
            return true;
        }
        if (attempt < kDeviceChangeAttempts) {
            Sleep(kDeviceChangeRetryDelayMilliseconds);
        }
    }
    return false;
}

bool VddDevice::try_set_enabled(bool enabled) {
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
