#pragma once

// Installer steps around the Virtual Display Driver (DEVPLAN Phase 14): the first driver device
// when Osplay is installed, and everything Osplay added when it is removed. Each needs an
// administrator command line and returns a process exit code.

namespace osplay::helper {

/// Adds a Root\MttVDD device when none exists. The driver package must be in the driver store
/// (pnputil /add-driver). Later devices are added by the service, one per virtual monitor.
[[nodiscard]] int ensure_vdd_device();

/// Removes every Root\MttVDD device and the driver settings file Osplay writes.
[[nodiscard]] int remove_vdd_devices();

/// Removes the Virtual Display Driver package from the driver store.
[[nodiscard]] int remove_vdd_driver_package();

} // namespace osplay::helper
