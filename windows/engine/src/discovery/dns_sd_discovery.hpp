#pragma once

// Bonjour on Windows through the system DNS-SD API (DnsServiceRegister / Browse / Resolve, DEVPLAN
// discovery): advertises this device's control port and finds other Osplay devices on the LAN.

#include <osplay/osplay.h>
#include <osplay/win/expected.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace osplay::engine {

enum class DiscoveryError : std::uint8_t {
    InvalidAdvertisement,
    Unavailable, ///< the DNS-SD service refused the request
};

/// An all-empty advertisement (a valid enum value, unlike zero-initialization).
inline osplay_advertisement blank_advertisement() {
    return {.protocol_version = 0,
            .platform = OSPLAY_PLATFORM_MACOS,
            .roles = 0,
            .protocol_minor = 0,
            .device_id = {},
            .name = {}};
}

struct DiscoveredDevice {
    std::string label; ///< identifies the device in on_lost
    osplay_advertisement advertisement = blank_advertisement();
    std::string address; ///< IPv4 or IPv6 literal, or a .local host name
    std::uint16_t port = 0;
};

class DnsSdBrowser {
public:
    struct Callbacks {
        std::function<void(const DiscoveredDevice&)> on_found; ///< also when its record changes
        std::function<void(const std::string& label)> on_lost;
    };

    /// Callbacks run on system thread pool threads, one at a time.
    [[nodiscard]] static win::Expected<std::unique_ptr<DnsSdBrowser>, DiscoveryError>
    start(Callbacks callbacks);
    ~DnsSdBrowser();
    DnsSdBrowser(const DnsSdBrowser&) = delete;
    DnsSdBrowser& operator=(const DnsSdBrowser&) = delete;
    DnsSdBrowser(DnsSdBrowser&&) = delete;
    DnsSdBrowser& operator=(DnsSdBrowser&&) = delete;

    struct State;

private:
    explicit DnsSdBrowser(State* state) : state_(state) {}
    void stop();
    State* state_; // outlives this object, see the destructor
};

class DnsSdAdvertiser {
public:
    [[nodiscard]] static win::Expected<std::unique_ptr<DnsSdAdvertiser>, DiscoveryError>
    start(const osplay_advertisement& advertisement, std::uint16_t port);
    ~DnsSdAdvertiser();
    DnsSdAdvertiser(const DnsSdAdvertiser&) = delete;
    DnsSdAdvertiser& operator=(const DnsSdAdvertiser&) = delete;
    DnsSdAdvertiser(DnsSdAdvertiser&&) = delete;
    DnsSdAdvertiser& operator=(DnsSdAdvertiser&&) = delete;

    struct State;

private:
    explicit DnsSdAdvertiser(State* state) : state_(state) {}
    State* state_; // outlives this object, see the destructor
};

} // namespace osplay::engine
