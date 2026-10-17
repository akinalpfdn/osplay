#include "service_host.hpp"

#include "file_log.hpp"
#include "helper_constants.hpp"
#include "pipe_server.hpp"
#include "vdd_device.hpp"
#include "vdd_settings_file.hpp"

#include <osplay/win/helper_request_handler.hpp>

#include <windows.h>
#include <wtsapi32.h>

#include <array>
#include <memory>
#include <string>

namespace osplay::helper {
namespace {

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept { CloseHandle(handle); }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

// The SCM calls plain C callbacks, so the running service's state lives here.
struct ServiceState {
    SERVICE_STATUS_HANDLE status_handle = nullptr;
    SERVICE_STATUS status{};
    UniqueHandle stop_event;
    UniqueHandle session_changed_event;
};
ServiceState g_state; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void report(DWORD state, DWORD wait_hint = 0) {
    g_state.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_state.status.dwCurrentState = state;
    g_state.status.dwControlsAccepted =
        state == SERVICE_RUNNING
            ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_SESSIONCHANGE
            : 0;
    g_state.status.dwWin32ExitCode = NO_ERROR;
    g_state.status.dwWaitHint = wait_hint;
    SetServiceStatus(g_state.status_handle, &g_state.status);
}

DWORD WINAPI control_handler(DWORD control, DWORD event_type, LPVOID /*event_data*/,
                             LPVOID /*context*/) {
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        report(SERVICE_STOP_PENDING, kServiceStopTimeoutMilliseconds);
        SetEvent(g_state.stop_event.get());
        return NO_ERROR;
    case SERVICE_CONTROL_SESSIONCHANGE:
        // The monitor belongs to the console user's session; it must not follow another user.
        if (event_type == WTS_CONSOLE_DISCONNECT || event_type == WTS_SESSION_LOGOFF) {
            SetEvent(g_state.session_changed_event.get());
        }
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI service_main(DWORD /*argc*/, LPWSTR* /*argv*/) {
    g_state.status_handle = RegisterServiceCtrlHandlerExW(kServiceName, control_handler, nullptr);
    if (g_state.status_handle == nullptr) {
        return;
    }
    report(SERVICE_START_PENDING, kServiceStopTimeoutMilliseconds);

    g_state.stop_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    g_state.session_changed_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!g_state.stop_event || !g_state.session_changed_event) {
        report(SERVICE_STOPPED);
        return;
    }

    FileLog log;
    VddDevice device(log);
    VddSettingsFile settings(log);
    win::helper::RequestHandler handler(device, settings, log);

    log.info("Service starting");
    handler.reset(); // a monitor left enabled by a crash or by the driver's installer goes away now
    report(SERVICE_RUNNING);

    PipeServer server(handler, log, g_state.stop_event.get(), g_state.session_changed_event.get());
    server.run();

    handler.reset();
    log.info("Service stopped");
    report(SERVICE_STOPPED);
}

} // namespace

bool run_as_service() {
    // The SCM takes a mutable name that must stay alive while the dispatcher runs.
    static std::wstring name(kServiceName);
    const std::array<SERVICE_TABLE_ENTRYW, 2> table{
        {{name.data(), service_main}, {nullptr, nullptr}}};
    return StartServiceCtrlDispatcherW(table.data()) != FALSE;
}

} // namespace osplay::helper
