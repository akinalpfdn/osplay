#include "vdd_setup.hpp"

#include "vdd_device.hpp"
#include "vdd_settings_file.hpp"

#include <osplay/win/vdd_device_lookup.hpp>

#include <windows.h>

#include <setupapi.h>

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace osplay::helper {
namespace {

/// The helper's log for command-line steps: the installer shows what went wrong.
class ConsoleLog final : public win::helper::HelperLog {
public:
    void info(std::string_view message) override { std::cout << message << '\n'; }
    void warning(std::string_view message) override { std::cerr << message << '\n'; }
    void error(std::string_view message) override { std::cerr << message << '\n'; }
};

/// The driver store's names (oemNN.inf) of the packages that install Root\MttVDD.
std::set<std::wstring> vdd_driver_packages() {
    std::set<std::wstring> packages;
    HDEVINFO raw_set = SetupDiCreateDeviceInfoList(&win::kDisplayDeviceClass, nullptr);
    if (raw_set == INVALID_HANDLE_VALUE) {
        return packages;
    }
    const win::DeviceInfoSet set(raw_set);
    // A device that is never registered, only to ask which drivers match the hardware ID.
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    std::wstring hardware_ids(win::kVddHardwareId);
    hardware_ids.append(2, L'\0');
    if (!SetupDiCreateDeviceInfoW(set.get(), L"DISPLAY", &win::kDisplayDeviceClass, nullptr,
                                  nullptr, DICD_GENERATE_ID, &device) ||
        !SetupDiSetDeviceRegistryPropertyW(
            set.get(), &device, SPDRP_HARDWAREID,
            reinterpret_cast<const BYTE*>(hardware_ids.data()),
            static_cast<DWORD>(hardware_ids.size() * sizeof(wchar_t))) ||
        !SetupDiBuildDriverInfoList(set.get(), &device, SPDIT_COMPATDRIVER)) {
        return packages;
    }
    SP_DRVINFO_DATA_W driver{};
    driver.cbSize = sizeof(driver);
    for (DWORD index = 0;
         SetupDiEnumDriverInfoW(set.get(), &device, SPDIT_COMPATDRIVER, index, &driver); ++index) {
        DWORD required = 0;
        SetupDiGetDriverInfoDetailW(set.get(), &device, &driver, nullptr, 0, &required);
        if (required < sizeof(SP_DRVINFO_DETAIL_DATA_W)) {
            continue;
        }
        std::vector<std::byte> buffer(required);
        auto* detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
        if (SetupDiGetDriverInfoDetailW(set.get(), &device, &driver, detail, required, nullptr)) {
            packages.insert(std::filesystem::path(std::data(detail->InfFileName)).filename());
        }
    }
    SetupDiDestroyDriverInfoList(set.get(), &device, SPDIT_COMPATDRIVER);
    return packages;
}

} // namespace

int ensure_vdd_device() {
    ConsoleLog log;
    VddDevices devices(log);
    if (devices.count() > 0) {
        return 0;
    }
    if (!devices.add()) {
        std::cerr << "Could not add the Virtual Display Driver device; is its driver package "
                     "installed (pnputil /add-driver MttVDD.inf)?\n";
        return 1;
    }
    std::cout << "Virtual Display Driver device added.\n";
    return 0;
}

int remove_vdd_devices() {
    int failures = 0;
    for (const std::wstring& instance : win::device_instances(win::kVddHardwareId)) {
        auto device = win::open_device(instance);
        if (!device || !SetupDiCallClassInstaller(DIF_REMOVE, device->set.get(), &device->data)) {
            ++failures;
        }
    }
    ConsoleLog log;
    VddSettingsFile settings(log);
    if (!settings.remove()) {
        ++failures;
    }
    if (failures > 0) {
        std::cerr << "Some Virtual Display Driver devices or settings could not be removed.\n";
        return 1;
    }
    std::cout << "Virtual Display Driver devices and settings removed.\n";
    return 0;
}

int remove_vdd_driver_package() {
    int failures = 0;
    for (const std::wstring& package : vdd_driver_packages()) {
        if (!SetupUninstallOEMInfW(package.c_str(), SUOI_FORCEDELETE, nullptr)) {
            ++failures;
        }
    }
    if (failures > 0) {
        std::cerr << "The Virtual Display Driver package could not be removed (error "
                  << GetLastError() << ").\n";
        return 1;
    }
    std::cout << "Virtual Display Driver package removed.\n";
    return 0;
}

} // namespace osplay::helper
