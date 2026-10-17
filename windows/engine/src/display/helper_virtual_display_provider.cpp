#include "helper_virtual_display_provider.hpp"

#include <utility>

namespace osplay::engine {

namespace helper = win::helper;

HelperVirtualDisplayProvider::HelperVirtualDisplayProvider(
    std::unique_ptr<HelperChannel> channel, std::unique_ptr<DisplayModeApplier> mode_applier)
    : channel_(std::move(channel)), mode_applier_(std::move(mode_applier)) {}

win::Status<DisplayError>
HelperVirtualDisplayProvider::create_monitor(const VirtualMonitorConfig& config) {
    if (!helper::is_valid(config)) {
        return win::Status<DisplayError>::failure(DisplayError::InvalidArgument);
    }
    const auto configured =
        send_expecting_ok({.command = helper::Command::Configure, .config = config});
    if (!configured) {
        return configured;
    }
    const auto enabled = send_expecting_ok({.command = helper::Command::Enable, .config = {}});
    if (!enabled) {
        return enabled;
    }
    // Windows gives a new monitor its own default mode; the exact mode is set from the user
    // session.
    const auto applied = mode_applier_->apply(config);
    if (!applied) {
        // A monitor with the wrong size is worse than none: remove it and report why.
        static_cast<void>(remove_monitor());
        return applied;
    }
    return win::Status<DisplayError>::ok();
}

win::Status<DisplayError> HelperVirtualDisplayProvider::remove_monitor() {
    return send_expecting_ok({.command = helper::Command::Disable, .config = {}});
}

win::Expected<VirtualMonitorStatus, DisplayError> HelperVirtualDisplayProvider::status() {
    using Result = win::Expected<VirtualMonitorStatus, DisplayError>;
    const auto response = send({.command = helper::Command::Status, .config = {}});
    if (!response) {
        return Result::failure(response.error());
    }
    if (response.value().result != helper::ResultCode::Ok) {
        return Result::failure(to_display_error(response.value().result));
    }
    return response.value().status;
}

win::Expected<helper::Response, DisplayError>
HelperVirtualDisplayProvider::send(const helper::Request& request) {
    using Result = win::Expected<helper::Response, DisplayError>;
    const helper::Bytes encoded = helper::encode(request);
    const auto reply = channel_->transact(encoded);
    if (!reply) {
        return Result::failure(reply.error());
    }
    const auto response = helper::decode_response(reply.value());
    if (!response) {
        return Result::failure(DisplayError::ProtocolMismatch);
    }
    return response.value();
}

win::Status<DisplayError>
HelperVirtualDisplayProvider::send_expecting_ok(const helper::Request& request) {
    const auto response = send(request);
    if (!response) {
        return win::Status<DisplayError>::failure(response.error());
    }
    if (response.value().result != helper::ResultCode::Ok) {
        return win::Status<DisplayError>::failure(to_display_error(response.value().result));
    }
    return win::Status<DisplayError>::ok();
}

DisplayError to_display_error(helper::ResultCode code) {
    switch (code) {
    case helper::ResultCode::VddNotInstalled:
        return DisplayError::VddNotInstalled;
    case helper::ResultCode::DeviceControlFailed:
        return DisplayError::DeviceControlFailed;
    case helper::ResultCode::ConfigWriteFailed:
        return DisplayError::ConfigWriteFailed;
    case helper::ResultCode::UnsupportedVersion:
        return DisplayError::ProtocolMismatch;
    case helper::ResultCode::ValueOutOfRange:
        return DisplayError::InvalidArgument;
    case helper::ResultCode::Ok:
    case helper::ResultCode::MalformedRequest:
    case helper::ResultCode::UnknownCommand:
    case helper::ResultCode::NotConfigured:
        return DisplayError::Rejected;
    }
    return DisplayError::Rejected;
}

} // namespace osplay::engine
