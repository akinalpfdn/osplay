#pragma once

#include <osplay/win/helper_request_handler.hpp>

namespace osplay::helper {

/// Enables and disables the Virtual Display Driver's root device through SetupAPI. Disabling
/// removes its monitor; the state persists across reboots (DICS_FLAG_GLOBAL).
class VddDevice final : public win::helper::VirtualMonitorDevice {
public:
    explicit VddDevice(win::helper::HelperLog& log) : log_(log) {}

    bool is_installed() override;
    bool is_enabled() override;
    bool set_enabled(bool enabled) override;

private:
    win::helper::HelperLog& log_;
};

} // namespace osplay::helper
