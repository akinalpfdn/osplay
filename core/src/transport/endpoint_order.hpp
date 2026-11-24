#pragma once

#include <asio/ip/tcp.hpp>

#include <algorithm>
#include <vector>

namespace osplay::transport {

/// The order a viewer tries a host's addresses in: IPv4 first. A host name on a LAN often also
/// resolves to an IPv6 link-local address that drops connections; tried first, it used up the
/// whole connect timeout before IPv4 was reached (2026-11-24). Keeps the resolver's order within
/// each family.
inline void prefer_ipv4(std::vector<asio::ip::tcp::endpoint>& endpoints) {
    std::ranges::stable_partition(endpoints, [](const asio::ip::tcp::endpoint& endpoint) {
        return endpoint.address().is_v4();
    });
}

} // namespace osplay::transport
