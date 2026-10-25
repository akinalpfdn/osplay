#include <osplay/win/helper_request_handler.hpp>
#include <osplay/win/vdd_settings.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

namespace osplay::win::helper {
namespace {

Response with(ResultCode result) {
    return Response{.result = result, .status = {}};
}

} // namespace

RequestHandler::RequestHandler(VirtualMonitorDevices& devices, SettingsStore& settings,
                               HelperLog& log)
    : devices_(devices), settings_(settings), log_(log) {}

Bytes RequestHandler::handle(std::span<const std::uint8_t> message) {
    const auto request = decode_request(message);
    if (!request) {
        log_.warning(std::string("Rejected request: ") + std::string(to_string(request.error())));
        return encode(with(result_for(request.error())));
    }
    return encode(handle(request.value()));
}

Response RequestHandler::handle(const Request& request) {
    switch (request.command) {
    case Command::Configure:
        return configure(request.monitors);
    case Command::Enable:
        return enable();
    case Command::Disable:
        return disable();
    case Command::Status:
        return status();
    }
    log_.warning("Rejected request: unknown command");
    return with(ResultCode::UnknownCommand);
}

void RequestHandler::reset() {
    monitors_.clear();
    if (!devices_.is_installed()) {
        return;
    }
    if (const auto removed = disable_from(0); !removed) {
        log_.error("Could not disable the virtual display devices during reset");
    } else if (*removed > 0) {
        log_.info("Virtual monitors removed (reset): " + std::to_string(*removed));
    }
}

Response RequestHandler::configure(const std::vector<MonitorConfig>& monitors) {
    // decode_request already validates; requests built in-process are checked here.
    if (!is_valid(monitors)) {
        log_.warning("Rejected configure: value out of range");
        return with(ResultCode::ValueOutOfRange);
    }
    if (!settings_.write(render_vdd_settings(monitors))) {
        log_.error("Could not write the virtual display driver settings");
        return with(ResultCode::ConfigWriteFailed);
    }
    monitors_ = monitors;
    std::string modes;
    for (const MonitorConfig& monitor : monitors) {
        modes += (modes.empty() ? "" : ", ") + std::to_string(monitor.width) + "x" +
                 std::to_string(monitor.height) + " @ " +
                 std::to_string(monitor.refresh_millihertz) + " mHz";
    }
    log_.info("Configured " + std::to_string(monitors.size()) + " monitor(s): " + modes);

    if (!devices_.is_installed()) {
        return with(ResultCode::Ok); // Enable reports the missing driver
    }
    // The driver reads its settings only when a device starts: running devices start again, and
    // the ones this configuration does not need stop.
    const std::size_t used = std::min(devices_.count(), monitors.size());
    for (std::size_t index = 0; index < used; ++index) {
        if (devices_.is_enabled(index) &&
            (!devices_.set_enabled(index, false) || !devices_.set_enabled(index, true))) {
            log_.error("Could not restart virtual display device " + std::to_string(index));
            return with(ResultCode::DeviceControlFailed);
        }
    }
    if (!disable_from(monitors.size())) {
        log_.error("Could not disable an unused virtual display device");
        return with(ResultCode::DeviceControlFailed);
    }
    return with(ResultCode::Ok);
}

Response RequestHandler::enable() {
    if (!devices_.is_installed()) {
        return with(ResultCode::VddNotInstalled);
    }
    if (monitors_.empty()) {
        return with(ResultCode::NotConfigured);
    }
    // One device per monitor; a new device starts at once with the current settings. Never more
    // additions than monitors are missing, even if a new device does not show up.
    for (std::size_t present = devices_.count(); present < monitors_.size(); ++present) {
        if (!devices_.add()) {
            log_.error("Could not add a virtual display device");
            return with(ResultCode::DeviceControlFailed);
        }
        log_.info("Virtual display device added: " + std::to_string(present + 1));
    }
    if (devices_.count() < monitors_.size()) {
        log_.error("Added virtual display devices did not appear");
        return with(ResultCode::DeviceControlFailed);
    }
    std::size_t added = 0;
    for (std::size_t index = 0; index < monitors_.size(); ++index) {
        if (devices_.is_enabled(index)) {
            continue;
        }
        if (!devices_.set_enabled(index, true)) {
            log_.error("Could not enable virtual display device " + std::to_string(index));
            return with(ResultCode::DeviceControlFailed);
        }
        ++added;
    }
    if (added > 0) {
        log_.info("Virtual monitors added: " + std::to_string(added));
    }
    return with(ResultCode::Ok);
}

Response RequestHandler::disable() {
    if (!devices_.is_installed()) {
        return with(ResultCode::VddNotInstalled);
    }
    const auto removed = disable_from(0);
    if (!removed) {
        log_.error("Could not disable the virtual display devices");
        return with(ResultCode::DeviceControlFailed);
    }
    if (*removed > 0) {
        log_.info("Virtual monitors removed: " + std::to_string(*removed));
    }
    return with(ResultCode::Ok);
}

Response RequestHandler::status() {
    Response response = with(ResultCode::Ok);
    response.status.vdd_installed = devices_.is_installed();
    response.status.device_enabled = response.status.vdd_installed && any_enabled();
    response.status.configured = !monitors_.empty();
    response.status.monitor_count = static_cast<std::uint8_t>(monitors_.size());
    if (!monitors_.empty()) {
        response.status.width = monitors_.front().width;
        response.status.height = monitors_.front().height;
        response.status.refresh_millihertz = monitors_.front().refresh_millihertz;
    }
    return response;
}

std::optional<std::size_t> RequestHandler::disable_from(std::size_t first) {
    std::size_t removed = 0;
    for (std::size_t index = first; index < devices_.count(); ++index) {
        if (!devices_.is_enabled(index)) {
            continue;
        }
        if (!devices_.set_enabled(index, false)) {
            return std::nullopt;
        }
        ++removed;
    }
    return removed;
}

bool RequestHandler::any_enabled() {
    for (std::size_t index = 0; index < devices_.count(); ++index) {
        if (devices_.is_enabled(index)) {
            return true;
        }
    }
    return false;
}

std::string_view to_string(DecodeError error) {
    switch (error) {
    case DecodeError::Truncated:
        return "truncated";
    case DecodeError::TooLarge:
        return "too large";
    case DecodeError::BadMagic:
        return "bad magic";
    case DecodeError::UnsupportedVersion:
        return "unsupported version";
    case DecodeError::UnknownCommand:
        return "unknown command";
    case DecodeError::LengthMismatch:
        return "length mismatch";
    case DecodeError::ValueOutOfRange:
        return "value out of range";
    case DecodeError::InvalidGpuName:
        return "invalid GPU name";
    }
    return "unknown error";
}

std::string_view to_string(Command command) {
    switch (command) {
    case Command::Configure:
        return "configure";
    case Command::Enable:
        return "enable";
    case Command::Disable:
        return "disable";
    case Command::Status:
        return "status";
    }
    return "unknown";
}

} // namespace osplay::win::helper
