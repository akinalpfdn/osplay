#include "service_installer.hpp"

#include "helper_constants.hpp"

#include <windows.h>

#include <array>
#include <iostream>
#include <memory>
#include <string>

namespace osplay::helper {
namespace {

struct ServiceHandleCloser {
    void operator()(SC_HANDLE handle) const noexcept { CloseServiceHandle(handle); }
};
using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ServiceHandleCloser>;

int fail(const char* what) {
    const DWORD error = GetLastError();
    std::cerr << what << " failed (error " << error << ")"
              << (error == ERROR_ACCESS_DENIED ? ": run from an administrator command line" : "")
              << '\n';
    return static_cast<int>(error == 0 ? 1 : error);
}

} // namespace

int install_service() {
    std::array<wchar_t, MAX_PATH> module_path{};
    const DWORD length =
        GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (length == 0 || length == module_path.size()) {
        return fail("GetModuleFileNameW");
    }
    // Quoted so a path with spaces cannot be misread as another executable.
    const std::wstring command = L"\"" + std::wstring(module_path.data(), length) + L"\"";

    const ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE));
    if (!manager) {
        return fail("OpenSCManagerW");
    }
    const ServiceHandle service(
        CreateServiceW(manager.get(), kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS,
                       SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                       command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr));
    if (!service) {
        if (GetLastError() == ERROR_SERVICE_EXISTS) {
            std::cerr << "The service is already installed. Run --uninstall first.\n";
            return static_cast<int>(ERROR_SERVICE_EXISTS);
        }
        return fail("CreateServiceW");
    }

    std::wstring description(kServiceDescription);
    SERVICE_DESCRIPTIONW description_info{description.data()};
    ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_DESCRIPTION, &description_info);

    std::array<SC_ACTION, 3> actions{{{SC_ACTION_RESTART, kServiceRestartDelayMilliseconds},
                                      {SC_ACTION_RESTART, kServiceRestartDelayMilliseconds},
                                      {SC_ACTION_NONE, 0}}};
    SERVICE_FAILURE_ACTIONSW failure{};
    failure.dwResetPeriod = 24 * 60 * 60;
    failure.cActions = static_cast<DWORD>(actions.size());
    failure.lpsaActions = actions.data();
    ChangeServiceConfig2W(service.get(), SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

    if (!StartServiceW(service.get(), 0, nullptr)) {
        return fail("StartServiceW");
    }
    std::cout << "OsplayHelper installed and started.\n";
    return 0;
}

int uninstall_service() {
    const ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) {
        return fail("OpenSCManagerW");
    }
    const ServiceHandle service(
        OpenServiceW(manager.get(), kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE));
    if (!service) {
        return fail("OpenServiceW");
    }

    SERVICE_STATUS status{};
    if (ControlService(service.get(), SERVICE_CONTROL_STOP, &status)) {
        const ULONGLONG deadline = GetTickCount64() + kServiceStopTimeoutMilliseconds;
        while (QueryServiceStatus(service.get(), &status) &&
               status.dwCurrentState != SERVICE_STOPPED && GetTickCount64() < deadline) {
            Sleep(250);
        }
    }
    if (!DeleteService(service.get())) {
        return fail("DeleteService");
    }
    std::cout << "OsplayHelper stopped and removed.\n";
    return 0;
}

} // namespace osplay::helper
