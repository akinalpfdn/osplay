#pragma once

// Every tunable of a session in one place (DEVPLAN: "tunables live in one constants file"); the
// defaults are the shipping values, tests shorten them.

#include "protocol/control_messages.hpp"
#include "session/bitrate_controller.hpp"
#include "util/time.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace osplay::session {

struct SessionTunables {
    Microseconds tick_interval{2'000};
    Microseconds handshake_timeout{10'000'000};
    /// Viewer: a host that does not accept the TCP connection within this time (asleep, gone, an
    /// old advertisement) ends the session with TIMED_OUT instead of the OS's minute-long wait.
    Microseconds connect_timeout{5'000'000};
    Microseconds heartbeat_interval{1'000'000};
    /// No control message for this long ends the session; must exceed brief network outages.
    Microseconds heartbeat_timeout{10'000'000};
    Microseconds ping_interval{1'000'000};
    Microseconds report_interval{1'000'000};
    Microseconds stats_interval{1'000'000};
    /// A summary line per stream goes to the log this often: enough to read a session back
    /// from a bug report without growing the log much (about 50 kB an hour per stream).
    Microseconds stats_log_interval{10'000'000};
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
    /// A role swap request without an answer for this long is given up (NoAnswer).
    Microseconds role_swap_timeout{10'000'000};
    /// Host: how receiver reports move each stream's bitrate.
    BitrateTunables bitrate;
    /// Host: a frame's fragments are spread over this share of the stream's frame interval
    /// (divided among the streams) instead of leaving in one burst, which Wi-Fi queues drop.
    double pacing_fraction = 0.5;
    /// Host: frames of at most this many fragments go out at once.
    std::size_t pacing_minimum_fragments = 4;
    /// Host: a frame also leaves no faster than this many times the streams' bitrate targets
    /// together, so a large keyframe is spread over tens of milliseconds instead of a few
    /// (libwebrtc's pacer uses 2.5 as well).
    double pacing_rate_factor = 2.5;
    /// Host: when this much is already queued, frames go out at the fraction window again so the
    /// queue drains instead of adding latency without end.
    Microseconds pacing_max_queue{100'000};
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
