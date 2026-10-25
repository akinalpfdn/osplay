#pragma once

namespace osplay::helper {

/// Registers the running executable as the auto-start OsplayHelper service and starts it. An
/// existing service is stopped and pointed at this executable instead. Requires administrator
/// rights. Returns a process exit code.
[[nodiscard]] int install_service();

/// Stops the OsplayHelper service and waits until it has, so an installer can replace its files.
/// Succeeds when the service is not installed or not running. Requires administrator rights.
[[nodiscard]] int stop_service();

/// Stops and deletes the OsplayHelper service. Requires administrator rights.
[[nodiscard]] int uninstall_service();

} // namespace osplay::helper
