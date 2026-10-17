#pragma once

namespace osplay::helper {

/// Connects to the Service Control Manager and runs the helper until it is stopped.
/// Returns false when the process was not started by the SCM.
[[nodiscard]] bool run_as_service();

} // namespace osplay::helper
