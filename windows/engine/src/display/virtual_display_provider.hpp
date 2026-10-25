#pragma once

#include <osplay/win/expected.hpp>
#include <osplay/win/helper_protocol.hpp>

#include <cstddef>
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
    /// Other host sessions use every virtual monitor this PC can have.
    NoMonitorLeft,
};

using VirtualMonitorConfig = win::helper::MonitorConfig;
using VirtualMonitorStatus = win::helper::MonitorStatus;

/// Adds and removes one host session's Windows virtual monitors. They exist only while the
/// provider is alive; destroying it removes them.
class VirtualDisplayProvider {
public:
    virtual ~VirtualDisplayProvider() = default;

    /// Replaces this provider's monitors with these (1...kMaxMonitors), each in its own mode, and
    /// returns their GDI names (\\.\DISPLAYn) in the same order. NoMonitorLeft when other
    /// sessions use too many.
    [[nodiscard]] virtual win::Expected<std::vector<std::wstring>, DisplayError>
    create_monitors(std::span<const VirtualMonitorConfig> configs) = 0;
    [[nodiscard]] virtual win::Status<DisplayError> remove_monitors() = 0;
    [[nodiscard]] virtual win::Expected<VirtualMonitorStatus, DisplayError> status() = 0;
    /// How many monitors create_monitors could make now: the free ones and this provider's own.
    /// Never blocks.
    [[nodiscard]] virtual std::size_t available_monitors() const = 0;
};

} // namespace osplay::engine
