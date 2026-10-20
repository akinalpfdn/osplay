#include <osplay/win/helper_request_handler.hpp>
#include <osplay/win/vdd_settings.hpp>

#include <string>

namespace osplay::win::helper {
namespace {

Response with(ResultCode result) {
    return Response{.result = result, .status = {}};
}

} // namespace

RequestHandler::RequestHandler(VirtualMonitorDevice& device, SettingsStore& settings,
                               HelperLog& log)
    : device_(device), settings_(settings), log_(log) {}

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
    if (device_.is_installed() && device_.is_enabled()) {
        if (device_.set_enabled(false)) {
            log_.info("Virtual monitor removed (reset)");
        } else {
            log_.error("Could not disable the virtual display device during reset");
        }
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

    // The driver reads its settings only when the device starts.
    if (device_.is_installed() && device_.is_enabled() && !restart_device()) {
        return with(ResultCode::DeviceControlFailed);
    }
    return with(ResultCode::Ok);
}

Response RequestHandler::enable() {
    if (!device_.is_installed()) {
        return with(ResultCode::VddNotInstalled);
    }
    if (monitors_.empty()) {
        return with(ResultCode::NotConfigured);
    }
    if (device_.is_enabled()) {
        return with(ResultCode::Ok);
    }
    if (!device_.set_enabled(true)) {
        log_.error("Could not enable the virtual display device");
        return with(ResultCode::DeviceControlFailed);
    }
    log_.info("Virtual monitor added");
    return with(ResultCode::Ok);
}

Response RequestHandler::disable() {
    if (!device_.is_installed()) {
        return with(ResultCode::VddNotInstalled);
    }
    if (!device_.is_enabled()) {
        return with(ResultCode::Ok);
    }
    if (!device_.set_enabled(false)) {
        log_.error("Could not disable the virtual display device");
        return with(ResultCode::DeviceControlFailed);
    }
    log_.info("Virtual monitor removed");
    return with(ResultCode::Ok);
}

Response RequestHandler::status() {
    Response response = with(ResultCode::Ok);
    response.status.vdd_installed = device_.is_installed();
    response.status.device_enabled = response.status.vdd_installed && device_.is_enabled();
    response.status.configured = !monitors_.empty();
    response.status.monitor_count = static_cast<std::uint8_t>(monitors_.size());
    if (!monitors_.empty()) {
        response.status.width = monitors_.front().width;
        response.status.height = monitors_.front().height;
        response.status.refresh_millihertz = monitors_.front().refresh_millihertz;
    }
    return response;
}

bool RequestHandler::restart_device() {
    if (!device_.set_enabled(false) || !device_.set_enabled(true)) {
        log_.error("Could not restart the virtual display device");
        return false;
    }
    log_.info("Virtual display device restarted to apply settings");
    return true;
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
