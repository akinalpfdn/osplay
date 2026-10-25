#pragma once

// The Windows host's virtual monitors through the OsplayHelper service, shared by every host
// session of this process: one helper connection (the helper serves one client), one driver
// device per monitor (DECISIONS.md, 2026-10-25). A session's monitors come and go without
// touching the other sessions'.

#include "virtual_display_provider.hpp"

#include <osplay/win/helper_protocol.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace osplay::engine {

/// One request/response exchange with the helper service.
class HelperChannel {
public:
    virtual ~HelperChannel() = default;
    [[nodiscard]] virtual win::Expected<win::helper::Bytes, DisplayError>
    transact(std::span<const std::uint8_t> request) = 0;
};

/// Finds the virtual monitors on the desktop and sets their modes. Display settings belong to the
/// user's session, so this runs in the engine, not in the session-0 helper service.
class DisplayModeApplier {
public:
    virtual ~DisplayModeApplier() = default;
    /// GDI names (\\.\DISPLAYn) of the virtual monitors on the desktop now; empty when none is.
    [[nodiscard]] virtual win::Expected<std::vector<std::wstring>, DisplayError>
    current_monitors() = 0;
    /// Waits for configs.size() monitors that are not in `existing`, sets their modes in order
    /// and returns their names in that order.
    [[nodiscard]] virtual win::Expected<std::vector<std::wstring>, DisplayError>
    apply(std::span<const VirtualMonitorConfig> configs,
          std::span<const std::wstring> existing) = 0;
};

class VirtualDisplayHub {
public:
    /// The channel stays open for the hub's lifetime; the helper removes every monitor when it
    /// closes.
    VirtualDisplayHub(std::unique_ptr<HelperChannel> channel,
                      std::unique_ptr<DisplayModeApplier> mode_applier);

    /// Replaces this owner's monitors with these on free driver devices; the other owners'
    /// monitors keep running.
    [[nodiscard]] win::Expected<std::vector<std::wstring>, DisplayError>
    create_monitors(const void* owner, std::span<const VirtualMonitorConfig> configs);
    [[nodiscard]] win::Status<DisplayError> remove_monitors(const void* owner);
    [[nodiscard]] win::Expected<VirtualMonitorStatus, DisplayError> status();
    /// Devices no owner uses; read without waiting for a create in progress.
    [[nodiscard]] std::size_t free_monitors() const;

private:
    struct Slot {
        const void* owner = nullptr; // nullptr: free
        VirtualMonitorConfig config;
    };

    /// Disables the owner's devices and frees them (also when the helper failed).
    win::Status<DisplayError> release(const void* owner);
    void forget(const void* owner);
    [[nodiscard]] std::uint8_t devices_of(const void* owner) const;
    [[nodiscard]] std::vector<VirtualMonitorConfig> modes_in_use() const;
    win::Expected<win::helper::Response, DisplayError> send(const win::helper::Request& request);
    win::Status<DisplayError> send_expecting_ok(const win::helper::Request& request);

    std::unique_ptr<HelperChannel> channel_;
    std::unique_ptr<DisplayModeApplier> mode_applier_;
    std::mutex mutex_;                                  // guards slots_ and the channel
    std::array<Slot, win::helper::kMaxMonitors> slots_; // index i: driver device i
    std::atomic<std::size_t> used_{0};
};

/// One host session's monitors on the hub; they go when it does.
class HubVirtualDisplays final : public VirtualDisplayProvider {
public:
    explicit HubVirtualDisplays(std::shared_ptr<VirtualDisplayHub> hub);
    ~HubVirtualDisplays() override;
    HubVirtualDisplays(const HubVirtualDisplays&) = delete;
    HubVirtualDisplays& operator=(const HubVirtualDisplays&) = delete;
    HubVirtualDisplays(HubVirtualDisplays&&) = delete;
    HubVirtualDisplays& operator=(HubVirtualDisplays&&) = delete;

    win::Expected<std::vector<std::wstring>, DisplayError>
    create_monitors(std::span<const VirtualMonitorConfig> configs) override;
    win::Status<DisplayError> remove_monitors() override;
    win::Expected<VirtualMonitorStatus, DisplayError> status() override;
    [[nodiscard]] std::size_t available_monitors() const override;

private:
    std::shared_ptr<VirtualDisplayHub> hub_;
    std::atomic<std::size_t> own_{0};
};

[[nodiscard]] DisplayError to_display_error(win::helper::ResultCode code);

} // namespace osplay::engine
