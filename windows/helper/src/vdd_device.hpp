#pragma once

#include <osplay/win/helper_request_handler.hpp>

#include <cstddef>
#include <string>

namespace osplay::helper {

/// The Virtual Display Driver's root devices through SetupAPI, in instance ID order. Disabling a
/// device removes its monitor; the state persists across reboots (DICS_FLAG_GLOBAL). New devices
/// get the driver already in the driver store.
class VddDevices final : public win::helper::VirtualMonitorDevices {
public:
    explicit VddDevices(win::helper::HelperLog& log) : log_(log) {}

    bool is_installed() override;
    std::size_t count() override;
    bool add() override;
    bool is_enabled(std::size_t index) override;
    bool set_enabled(std::size_t index, bool enabled) override;

private:
    bool try_set_enabled(const std::wstring& instance, bool enabled);

    win::helper::HelperLog& log_;
};

} // namespace osplay::helper
