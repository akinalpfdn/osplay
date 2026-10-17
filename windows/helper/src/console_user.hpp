#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace osplay::helper {

/// String SID of the user logged on at the physical console, if any.
[[nodiscard]] std::optional<std::wstring> console_user_sid();

/// True when the process on the other end of `pipe` runs as the console user (or LocalSystem)
/// inside the active console session. Defense in depth on top of the pipe's ACL.
[[nodiscard]] bool pipe_client_is_console_user(HANDLE pipe, const std::wstring& console_sid);

} // namespace osplay::helper
