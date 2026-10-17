#pragma once

namespace osplay::helper {

/// Registers the running executable as the auto-start OsplayHelper service and starts it.
/// Requires administrator rights. Returns a process exit code.
[[nodiscard]] int install_service();

/// Stops and deletes the OsplayHelper service. Requires administrator rights.
[[nodiscard]] int uninstall_service();

} // namespace osplay::helper
