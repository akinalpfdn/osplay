#pragma once

#include "virtual_display_provider.hpp"

#include <osplay/win/helper_protocol.hpp>

#include <memory>
#include <span>

namespace osplay::engine {

/// One request/response exchange with the helper service.
class HelperChannel {
public:
    virtual ~HelperChannel() = default;
    [[nodiscard]] virtual win::Expected<win::helper::Bytes, DisplayError>
    transact(std::span<const std::uint8_t> request) = 0;
};

/// VirtualDisplayProvider backed by the OsplayHelper service. The channel stays open for the
/// provider's lifetime; the helper removes the monitor when it closes.
class HelperVirtualDisplayProvider final : public VirtualDisplayProvider {
public:
    explicit HelperVirtualDisplayProvider(std::unique_ptr<HelperChannel> channel);

    win::Status<DisplayError> create_monitor(const VirtualMonitorConfig& config) override;
    win::Status<DisplayError> remove_monitor() override;
    win::Expected<VirtualMonitorStatus, DisplayError> status() override;

private:
    win::Expected<win::helper::Response, DisplayError> send(const win::helper::Request& request);
    win::Status<DisplayError> send_expecting_ok(const win::helper::Request& request);

    std::unique_ptr<HelperChannel> channel_;
};

[[nodiscard]] DisplayError to_display_error(win::helper::ResultCode code);

} // namespace osplay::engine
