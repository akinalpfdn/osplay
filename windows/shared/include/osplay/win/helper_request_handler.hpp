#pragma once

#include <osplay/win/helper_protocol.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace osplay::win::helper {

/// The Virtual Display Driver device: present or not, enabled or disabled.
class VirtualMonitorDevice {
public:
    virtual ~VirtualMonitorDevice() = default;
    [[nodiscard]] virtual bool is_installed() = 0;
    [[nodiscard]] virtual bool is_enabled() = 0;
    /// Persistent across reboots. Returns false when the device could not be changed.
    [[nodiscard]] virtual bool set_enabled(bool enabled) = 0;
};

/// Where the driver reads its settings from.
class SettingsStore {
public:
    virtual ~SettingsStore() = default;
    [[nodiscard]] virtual bool write(const std::string& contents) = 0;
};

class HelperLog {
public:
    virtual ~HelperLog() = default;
    virtual void info(std::string_view message) = 0;
    virtual void warning(std::string_view message) = 0;
    virtual void error(std::string_view message) = 0;
};

/// Executes helper commands. One instance serves one connected client at a time; the virtual
/// monitor exists only while that client's session is active.
class RequestHandler {
public:
    RequestHandler(VirtualMonitorDevice& device, SettingsStore& settings, HelperLog& log);

    /// Decodes, validates and executes one request message; always returns an encoded response.
    [[nodiscard]] Bytes handle(std::span<const std::uint8_t> message);
    [[nodiscard]] Response handle(const Request& request);

    /// Removes the virtual monitor and forgets the configuration. Called when the service starts
    /// (to clear a monitor left behind by a crash) and whenever the client disconnects.
    void reset();

private:
    Response configure(const MonitorConfig& config);
    Response enable();
    Response disable();
    Response status();
    bool restart_device();

    VirtualMonitorDevice& device_;
    SettingsStore& settings_;
    HelperLog& log_;
    std::optional<MonitorConfig> config_;
};

[[nodiscard]] std::string_view to_string(DecodeError error);
[[nodiscard]] std::string_view to_string(Command command);

} // namespace osplay::win::helper
