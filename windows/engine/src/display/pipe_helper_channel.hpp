#pragma once

#include "virtual_display_hub.hpp"

#include <windows.h>

#include <memory>

namespace osplay::engine {

/// HelperChannel over \\.\pipe\OsplayHelper.
class PipeHelperChannel final : public HelperChannel {
    struct ConstructionToken {}; // only connect() can construct

public:
    PipeHelperChannel(ConstructionToken /*token*/, HANDLE pipe) : pipe_(pipe) {}

    /// Connects and verifies that the server is a service (session 0), not a user process that
    /// registered the pipe name first.
    [[nodiscard]] static win::Expected<std::unique_ptr<PipeHelperChannel>, DisplayError> connect();

    ~PipeHelperChannel() override;
    PipeHelperChannel(const PipeHelperChannel&) = delete;
    PipeHelperChannel& operator=(const PipeHelperChannel&) = delete;
    PipeHelperChannel(PipeHelperChannel&&) = delete;
    PipeHelperChannel& operator=(PipeHelperChannel&&) = delete;

    win::Expected<win::helper::Bytes, DisplayError>
    transact(std::span<const std::uint8_t> request) override;

private:
    HANDLE pipe_;
};

} // namespace osplay::engine
