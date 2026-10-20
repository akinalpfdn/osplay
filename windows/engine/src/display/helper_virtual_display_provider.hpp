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

/// Applies each configured mode to its virtual monitor once they exist, and returns their GDI
/// names in order. Display settings belong to the user's session, so this runs in the engine, not
/// in the session-0 helper service.
class DisplayModeApplier {
public:
    virtual ~DisplayModeApplier() = default;
    [[nodiscard]] virtual win::Expected<std::vector<std::wstring>, DisplayError>
    apply(std::span<const VirtualMonitorConfig> configs) = 0;
};

/// VirtualDisplayProvider backed by the OsplayHelper service. The channel stays open for the
/// provider's lifetime; the helper removes the monitor when it closes.
class HelperVirtualDisplayProvider final : public VirtualDisplayProvider {
public:
    HelperVirtualDisplayProvider(std::unique_ptr<HelperChannel> channel,
                                 std::unique_ptr<DisplayModeApplier> mode_applier);

    win::Expected<std::vector<std::wstring>, DisplayError>
    create_monitors(std::span<const VirtualMonitorConfig> configs) override;
    win::Status<DisplayError> remove_monitors() override;
    win::Expected<VirtualMonitorStatus, DisplayError> status() override;

private:
    win::Expected<win::helper::Response, DisplayError> send(const win::helper::Request& request);
    win::Status<DisplayError> send_expecting_ok(const win::helper::Request& request);

    std::unique_ptr<HelperChannel> channel_;
    std::unique_ptr<DisplayModeApplier> mode_applier_;
};

[[nodiscard]] DisplayError to_display_error(win::helper::ResultCode code);

} // namespace osplay::engine
