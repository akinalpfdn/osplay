#pragma once

#include <osplay/win/expected.hpp>
#include <osplay/win/helper_protocol.hpp>

#include <cstdint>

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

/// Adds and removes the single Windows virtual monitor. The monitor exists only while the
/// provider is alive; destroying it removes the monitor.
class VirtualDisplayProvider {
public:
    virtual ~VirtualDisplayProvider() = default;

    [[nodiscard]] virtual win::Status<DisplayError>
    create_monitor(const VirtualMonitorConfig& config) = 0;
    [[nodiscard]] virtual win::Status<DisplayError> remove_monitor() = 0;
    [[nodiscard]] virtual win::Expected<VirtualMonitorStatus, DisplayError> status() = 0;
};

} // namespace osplay::engine
