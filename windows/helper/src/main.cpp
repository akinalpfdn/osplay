// OsplayHelper: privileged Windows service that adds and removes the virtual monitors.
//   OsplayHelper.exe --install        add the first driver device if none exists, then register
//                                     the auto-start service and start it (admin)
//   OsplayHelper.exe --uninstall      stop and remove the service, every driver device and the
//                                     driver settings (admin)
//   OsplayHelper.exe --remove-driver  remove the driver package from the driver store (admin)
//   OsplayHelper.exe                  entry point used by the Service Control Manager
#include "service_host.hpp"
#include "service_installer.hpp"
#include "vdd_setup.hpp"

#include <windows.h>

#include <cwchar>
#include <iostream>

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(misc-use-internal-linkage,*-avoid-c-arrays)
int wmain(int argc, wchar_t* argv[]) {
    if (argc == 2 && std::wcscmp(argv[1], L"--install") == 0) {
        // The service disables the new device when it starts, so no stray monitor stays.
        if (const int device = osplay::helper::ensure_vdd_device(); device != 0) {
            return device;
        }
        return osplay::helper::install_service();
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--uninstall") == 0) {
        const int service = osplay::helper::uninstall_service();
        const int devices = osplay::helper::remove_vdd_devices();
        return service != 0 ? service : devices;
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--remove-driver") == 0) {
        return osplay::helper::remove_vdd_driver_package();
    }
    if (argc == 1) {
        if (osplay::helper::run_as_service()) {
            return 0;
        }
        if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            std::cerr << "OsplayHelper runs as a Windows service. Use --install or --uninstall.\n";
            return 2;
        }
        return static_cast<int>(GetLastError());
    }
    std::cerr << "Usage: OsplayHelper.exe [--install | --uninstall | --remove-driver]\n";
    return 2;
}
