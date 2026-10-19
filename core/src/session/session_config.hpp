#pragma once

// Every tunable of a session in one place (DEVPLAN: "tunables live in one constants file"); the
// defaults are the shipping values, tests shorten them.

#include "protocol/control_messages.hpp"
#include "util/time.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace osplay::session {

struct SessionTunables {
    Microseconds tick_interval{2'000};
    Microseconds handshake_timeout{10'000'000};
    Microseconds heartbeat_interval{1'000'000};
    /// No control message for this long ends the session; must exceed brief network outages.
    Microseconds heartbeat_timeout{10'000'000};
    Microseconds ping_interval{1'000'000};
    Microseconds report_interval{1'000'000};
    Microseconds stats_interval{1'000'000};
    Microseconds udp_hello_interval{250'000};
    /// Viewer: minimum spacing of KeyframeRequests per stream.
    Microseconds keyframe_request_interval{250'000};
    /// Host: requests closer together than this produce one forced IDR.
    Microseconds keyframe_coalesce_interval{100'000};
    /// Host: after a stream's last frame, its final fragment is sent again at these delays (from
    /// that frame) unless a newer frame went out. A viewer that lost the whole frame learns of it
    /// and NACKs the rest; without a later frame the loss would go unnoticed.
    std::array<Microseconds, 3> tail_repeat_delays{Microseconds{50'000}, Microseconds{200'000},
                                                   Microseconds{700'000}};
    std::size_t retransmit_frames = 16;
    std::size_t retransmit_bytes = std::size_t{8} * 1024 * 1024;
    std::size_t max_frames_in_flight = 8;
    /// Viewer: positions naming a shape it does not have for this long trigger a keyframe
    /// request, which brings the shape again. Covers a shape change still in flight.
    Microseconds missing_cursor_shape_grace{100'000};
};

struct SessionConfig {
    protocol::Role role = protocol::Role::Host;
    protocol::Platform platform = protocol::Platform::MacOS;
    std::string device_name;
    protocol::Capabilities capabilities;
    /// Viewer: the monitors it lends (ViewerMonitors).
    std::vector<protocol::Monitor> monitors;
    SessionTunables tunables;
};

} // namespace osplay::session
