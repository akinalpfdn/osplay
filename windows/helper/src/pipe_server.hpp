#pragma once

#include <osplay/win/helper_request_handler.hpp>

#include <windows.h>

namespace osplay::helper {

/// Serves the helper protocol on \\.\pipe\OsplayHelper to one client at a time.
///
/// The pipe's ACL admits only SYSTEM and the console user; each client is verified again after
/// connecting. When the client disconnects, the console session changes or the service stops,
/// the request handler is reset, which removes the virtual monitor.
class PipeServer {
public:
    PipeServer(win::helper::RequestHandler& handler, win::helper::HelperLog& log, HANDLE stop_event,
               HANDLE session_changed_event);

    /// Blocks until `stop_event` is signaled.
    void run();

private:
    enum class Wait { Completed, Failed, Stopped, SessionChanged };

    void serve_one_client();
    void serve(HANDLE pipe);
    Wait wait_for(HANDLE pipe, OVERLAPPED& overlapped, DWORD& transferred, bool watch_session);

    win::helper::RequestHandler& handler_;
    win::helper::HelperLog& log_;
    HANDLE stop_event_;
    HANDLE session_changed_event_;
};

} // namespace osplay::helper
