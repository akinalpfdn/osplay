#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace osplay::helper {

/// ASCII-only conversion for SIDs and other identifiers in log lines.
[[nodiscard]] std::string narrow(const std::wstring& text);

/// String SID of the user logged on at the physical console, if any.
[[nodiscard]] std::optional<std::wstring> console_user_sid();

struct ClientCheck {
    bool accepted = false;
    std::string reason; // why the client was refused, for the log
};

/// Accepts the process on the other end of `pipe` only if it runs as the console user (or
/// LocalSystem) inside the active console session. Defense in depth on top of the pipe's ACL.
[[nodiscard]] ClientCheck check_pipe_client(HANDLE pipe, const std::wstring& console_sid);

} // namespace osplay::helper
