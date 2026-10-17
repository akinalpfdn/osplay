// OsplayHelper: privileged Windows service that adds and removes the virtual monitor.
//   OsplayHelper.exe --install     register as an auto-start service and start it (admin)
//   OsplayHelper.exe --uninstall   stop and remove the service (admin)
//   OsplayHelper.exe               entry point used by the Service Control Manager
#include "service_host.hpp"
#include "service_installer.hpp"

#include <windows.h>

#include <cwchar>
#include <iostream>

// Entry point: the signature is fixed by the C runtime.
// NOLINTNEXTLINE(misc-use-internal-linkage,*-avoid-c-arrays)
int wmain(int argc, wchar_t* argv[]) {
    if (argc == 2 && std::wcscmp(argv[1], L"--install") == 0) {
        return osplay::helper::install_service();
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--uninstall") == 0) {
        return osplay::helper::uninstall_service();
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
    std::cerr << "Usage: OsplayHelper.exe [--install | --uninstall]\n";
    return 2;
}
