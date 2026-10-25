#pragma once

// Tells the operating system what kind of traffic a session's sockets carry, so Wi-Fi treats it as
// latency sensitive: on macOS the network service type selects the WMM access category and lets
// the system know a real-time flow is running. On a Mac, Wi-Fi drops from AWDL channel hopping
// (AirDrop, Sidecar) were the remaining loss on a quiet network (DECISIONS.md, 2026-10-25).

#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>

namespace osplay::transport {

/// Marks the media socket as interactive video. Best effort: false where the platform has no such
/// option (Windows, Linux) or refuses it.
bool mark_as_interactive_video(asio::ip::udp::socket& socket);

/// Marks the control socket as signaling (NACKs, pings, stream control). Best effort as above.
bool mark_as_signaling(asio::ip::tcp::socket& socket);

} // namespace osplay::transport
