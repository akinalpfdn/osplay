#pragma once

#include <osplay/win/helper_request_handler.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace osplay::helper {

/// The Virtual Display Driver's root devices through SetupAPI. Disabling a device removes its
/// monitor; the state persists across reboots (DICS_FLAG_GLOBAL). New devices get the driver
/// already in the driver store. Every device runs in a driver host process of its own: pooled
/// devices share the driver's globals, and this driver keeps its monitor modes in a global list it
/// never clears, so a device started later would offer only an earlier device's modes. A device
/// keeps its index while the service runs: the engine names devices by index while other sessions'
/// monitors run on them, and a device added later can get a lower instance ID (a gap left by a
/// removed one) without moving the others.
class VddDevices final : public win::helper::VirtualMonitorDevices {
public:
    explicit VddDevices(win::helper::HelperLog& log) : log_(log) {}

    bool is_installed() override;
    std::size_t count() override;
    bool add() override;
    bool is_enabled(std::size_t index) override;
    bool set_enabled(std::size_t index, bool enabled) override;

private:
    /// The devices in their lasting order: the known ones as before, new ones at the end. A device
    /// seen for the first time gets its own driver host process.
    const std::vector<std::wstring>& instances();
    void isolate(const std::wstring& instance);
    bool try_set_enabled(const std::wstring& instance, bool enabled);

    win::helper::HelperLog& log_;
    std::vector<std::wstring> order_;
};

} // namespace osplay::helper
