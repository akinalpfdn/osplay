#pragma once

#include <osplay/win/expected.hpp>
#include <osplay/win/helper_protocol.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace osplay::engine {

enum class DisplayError : std::uint8_t {
    InvalidArgument,
    HelperUnavailable,
    HelperBusy,
    HelperAccessDenied,
    HelperNotTrusted,
    ProtocolMismatch,
    ConnectionLost,
    VddNotInstalled,
    DeviceControlFailed,
    ConfigWriteFailed,
    Rejected,
    MonitorNotFound,
    ModeChangeFailed,
};

using VirtualMonitorConfig = win::helper::MonitorConfig;
using VirtualMonitorStatus = win::helper::MonitorStatus;

/// Adds and removes the Windows virtual monitors. They exist only while the provider is alive;
/// destroying it removes them.
class VirtualDisplayProvider {
public:
    virtual ~VirtualDisplayProvider() = default;

    /// Replaces any monitors with these (1...kMaxMonitors), each in its own mode, and returns
    /// their GDI names (\\.\DISPLAYn) in the same order.
    [[nodiscard]] virtual win::Expected<std::vector<std::wstring>, DisplayError>
    create_monitors(std::span<const VirtualMonitorConfig> configs) = 0;
    [[nodiscard]] virtual win::Status<DisplayError> remove_monitors() = 0;
    [[nodiscard]] virtual win::Expected<VirtualMonitorStatus, DisplayError> status() = 0;
};

} // namespace osplay::engine
