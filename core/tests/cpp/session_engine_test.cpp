#include "session_simulation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace {

namespace protocol = osplay::protocol;
namespace session = osplay::session;
using osplay::Error;
using osplay::Microseconds;
using osplay::test::kPlaintextMarker;
using osplay::test::Peer;
using osplay::test::SessionState;
using osplay::test::SimulatedNetwork;
using osplay::test::TrustDecision;
using std::chrono::milliseconds;
using std::chrono::seconds;

protocol::Capabilities capabilities() {
    return {.codecs = protocol::kCodecH264AnnexB,
            .max_streams = 2,
            .flags = 0,
            .max_width = 7680,
            .max_height = 4320,
            .max_refresh_hz = 240};
}

session::SessionConfig host_config() {
    return {.role = protocol::Role::Host,
            .platform = protocol::Platform::MacOS,
            .device_name = "Test Host",
            .capabilities = capabilities(),
            .monitors = {},
            .tunables = {}};
}

session::SessionConfig viewer_config() {
    return {.role = protocol::Role::Viewer,
            .platform = protocol::Platform::Windows,
            .device_name = "Test Viewer",
            .capabilities = capabilities(),
            .monitors = {{.monitor_id = 0,
                          .width_px = 2560,
                          .height_px = 1440,
                          .refresh_millihertz = 60'000,
                          .physical_width_mm = 597,
                          .physical_height_mm = 336,
                          .scale_percent = 100,
                          .name = "Test"}},
            .tunables = {}};
}

/// Two engines on a simulated network, with a host-side "encoder" producing 60 fps synthetic
/// frames and honouring keyframe requests.
struct Simulation {
    explicit Simulation(TrustDecision host_trust = TrustDecision::Trusted,
                        TrustDecision viewer_trust = TrustDecision::Trusted)
        : host(network, SimulatedNetwork::kHost, host_config(), 1),
          viewer(network, SimulatedNetwork::kViewer, viewer_config(), 2) {
        network.attach(&host, &viewer);
        host.observer.trust = host_trust;
        viewer.observer.trust = viewer_trust;
    }

    void connect() {
        host.engine.on_connected(network.now);
        viewer.engine.on_connected(network.now);
    }

    void run_for(Microseconds duration) {
        const auto end = network.now + duration;
        while (network.now < end) {
            network.step();
            react();
        }
    }

    void react() {
        const auto encrypted = [](SessionState state) {
            return state != SessionState::Connecting && state != SessionState::Handshaking;
        };
        if (network.wire_at_handshake == 0 && encrypted(host.engine.state()) &&
            encrypted(viewer.engine.state())) {
            network.wire_at_handshake = network.wire.size();
        }
        if (!configured && !host.observer.monitors.empty() &&
            host.engine.state() == SessionState::Negotiating) {
            configured = true;
            const auto& monitor = host.observer.monitors.front();
            const auto result =
                host.engine.configure_streams({{.stream_id = 0,
                                                .monitor_id = monitor.monitor_id,
                                                .width = monitor.width_px,
                                                .height = monitor.height_px,
                                                .refresh_millihertz = monitor.refresh_millihertz,
                                                .codec = protocol::Codec::H264,
                                                .profile = protocol::H264Profile::High,
                                                .cursor_mode = protocol::CursorMode::InVideo,
                                                .bitrate_kbps = 20'000}},
                                              network.now);
            EXPECT_TRUE(result.has_value());
        }
        if (host.engine.state() == SessionState::Streaming && network.now >= next_frame) {
            if (host.observer.keyframe_requests > keyframe_requests_seen) {
                keyframe_requests_seen = host.observer.keyframe_requests;
                keyframe_pending = true;
            }
            const auto bytes = osplay::test::synthetic_frame(next_index, keyframe_pending);
            const auto pushed_result =
                host.engine.push_access_unit({.stream_id = 0,
                                              .keyframe = keyframe_pending,
                                              .codec_config = keyframe_pending,
                                              .capture_time_us = 0,
                                              .data = bytes},
                                             network.now);
            EXPECT_TRUE(pushed_result.has_value());
            pushed.push_back(next_index++);
            keyframe_pending = false;
            next_frame = network.now + Microseconds{16'667};
        }
    }

    [[nodiscard]] double delivered_ratio() const {
        return pushed.empty() ? 0.0
                              : static_cast<double>(viewer.observer.frame_indices.size()) /
                                    static_cast<double>(pushed.size());
    }

    SimulatedNetwork network;
    Peer host;
    Peer viewer;
    bool configured = false;
    bool keyframe_pending = true;
    int keyframe_requests_seen = 0;
    std::uint32_t next_index = 0;
    osplay::TimePoint next_frame{};
    std::vector<std::uint32_t> pushed;
};

TEST(SessionEngine, TrustedPeersReachStreamingAndDeliverEveryFrameOnACleanLink) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(6));

    EXPECT_EQ(sim.host.engine.state(), SessionState::Streaming);
    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Streaming);
    EXPECT_TRUE(sim.host.observer.pairing_codes.empty()) << "trusted peers skip the code";
    ASSERT_GT(sim.pushed.size(), 300U);
    // Everything except frames still in flight at the end arrives, in order and intact.
    EXPECT_GE(sim.viewer.observer.frame_indices.size() + 2, sim.pushed.size());
    EXPECT_TRUE(std::ranges::is_sorted(sim.viewer.observer.frame_indices));
    EXPECT_TRUE(sim.viewer.observer.frames_intact);
    ASSERT_TRUE(sim.viewer.observer.stats.has_value());
    EXPECT_EQ(sim.viewer.observer.stats->streams.at(0).frames_dropped, 0U);
    EXPECT_GT(sim.viewer.observer.stats->rtt_us, 0U);
}

TEST(SessionEngine, RecoversFromFivePercentDatagramLossThroughNacksAndKeyframes) {
    Simulation sim;
    sim.network.media_loss = 0.05;
    sim.connect();
    sim.run_for(seconds(15));

    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Streaming);
    ASSERT_TRUE(sim.host.observer.stats.has_value());
    EXPECT_GT(sim.host.observer.stats->streams.at(0).retransmitted_fragments, 0U)
        << "NACKs were answered";
    EXPECT_GE(sim.delivered_ratio(), 0.85);
    EXPECT_TRUE(sim.viewer.observer.frames_intact);
    // The stream is live at the end, not stuck waiting.
    ASSERT_FALSE(sim.viewer.observer.frame_indices.empty());
    EXPECT_GE(sim.viewer.observer.frame_indices.back() + 30, sim.pushed.back());
}

TEST(SessionEngine, SurvivesAThreeSecondNetworkInterruption) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(3));
    ASSERT_EQ(sim.viewer.engine.state(), SessionState::Streaming);

    sim.network.outage_start = sim.network.now;
    sim.network.outage_end = sim.network.now + seconds(3);
    sim.run_for(seconds(3));
    const std::size_t delivered_during_outage = sim.viewer.observer.frame_indices.size();
    sim.run_for(seconds(2));

    EXPECT_EQ(sim.host.engine.state(), SessionState::Streaming);
    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Streaming);
    EXPECT_GT(sim.viewer.observer.frame_indices.size(), delivered_during_outage + 60)
        << "streaming resumed";
    EXPECT_GE(sim.viewer.observer.frame_indices.back() + 30, sim.pushed.back()) << "back to live";
}

TEST(SessionEngine, NoPlaintextLeavesAfterTheHandshake) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(2));
    ASSERT_GT(sim.viewer.observer.frame_indices.size(), 30U);
    ASSERT_GT(sim.network.wire_at_handshake, 0U);

    for (std::size_t i = sim.network.wire_at_handshake; i < sim.network.wire.size(); ++i) {
        const auto& bytes = sim.network.wire[i];
        EXPECT_EQ(std::ranges::search(bytes, kPlaintextMarker).size(), 0U)
            << "plaintext frame data on the wire";
        const std::string text(bytes.begin(), bytes.end());
        EXPECT_EQ(text.find("Test Viewer"), std::string::npos) << "device name after the handshake";
    }
}

TEST(SessionEngine, FirstPairingShowsTheSameCodeAndStoresBothKeys) {
    Simulation sim(TrustDecision::Unknown, TrustDecision::Unknown);
    sim.connect();
    sim.run_for(milliseconds(100));
    ASSERT_EQ(sim.host.observer.pairing_codes.size(), 1U);
    ASSERT_EQ(sim.viewer.observer.pairing_codes.size(), 1U);
    EXPECT_EQ(sim.host.observer.pairing_codes[0], sim.viewer.observer.pairing_codes[0]);
    EXPECT_EQ(sim.host.engine.state(), SessionState::AwaitingPairing);

    ASSERT_TRUE(sim.host.engine.confirm_pairing(true, sim.network.now).has_value());
    sim.run_for(milliseconds(100));
    EXPECT_EQ(sim.host.engine.state(), SessionState::AwaitingPairing) << "waits for the other user";
    ASSERT_TRUE(sim.viewer.engine.confirm_pairing(true, sim.network.now).has_value());
    sim.run_for(seconds(1));

    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Streaming);
    EXPECT_EQ(sim.host.observer.paired.size(), 1U);
    EXPECT_EQ(sim.viewer.observer.paired.size(), 1U);
    EXPECT_FALSE(sim.host.engine.confirm_pairing(true, sim.network.now).has_value())
        << "decided already";
}

TEST(SessionEngine, RejectedPairingClosesBothSides) {
    Simulation sim(TrustDecision::Unknown, TrustDecision::Unknown);
    sim.connect();
    sim.run_for(milliseconds(100));
    ASSERT_TRUE(sim.viewer.engine.confirm_pairing(false, sim.network.now).has_value());
    sim.run_for(milliseconds(100));

    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Closed);
    EXPECT_EQ(sim.host.engine.state(), SessionState::Closed);
    EXPECT_EQ(sim.viewer.observer.close_reason, Error::PairingRejected);
    EXPECT_EQ(sim.host.observer.close_reason, Error::PairingRejected);
    EXPECT_TRUE(sim.host.observer.paired.empty());
}

TEST(SessionEngine, BlockedPeersAreRefused) {
    Simulation sim(TrustDecision::Blocked, TrustDecision::Unknown);
    sim.connect();
    sim.run_for(milliseconds(100));

    EXPECT_EQ(sim.host.observer.close_reason, Error::NotTrusted);
    EXPECT_EQ(sim.viewer.observer.close_reason, Error::NotTrusted);
    EXPECT_TRUE(sim.viewer.observer.pairing_codes.size() <= 1U);
}

TEST(SessionEngine, IncompatibleVersionsEndTheSessionWithASpecificError) {
    Simulation sim;
    sim.host.engine.on_connected(sim.network.now);
    // A Hello from a version-2 peer: magic, version 2, type Hello, ...
    const std::vector<std::uint8_t> frame{9, 0, 0, 0, 'O', 'P', 2, 0x01, 2, 2, 0, 0, 0};
    sim.host.engine.on_control_bytes(frame, sim.network.now);

    EXPECT_EQ(sim.host.engine.state(), SessionState::Closed);
    EXPECT_EQ(sim.host.observer.close_reason, Error::UnsupportedVersion);
}

TEST(SessionEngine, ASilentPeerTimesOut) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(2));
    ASSERT_EQ(sim.viewer.engine.state(), SessionState::Streaming);
    sim.network.outage_start = sim.network.now;
    sim.network.outage_end = sim.network.now + seconds(60);
    sim.run_for(seconds(12));

    EXPECT_EQ(sim.viewer.observer.close_reason, Error::TimedOut);
    EXPECT_EQ(sim.host.observer.close_reason, Error::TimedOut);
}

TEST(SessionEngine, PausingStopsTheStreamAndResumingRestartsItWithAKeyframe) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(2));
    ASSERT_TRUE(sim.viewer.engine.pause_stream(0, sim.network.now).has_value());
    sim.run_for(milliseconds(200));
    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Paused);
    EXPECT_EQ(sim.host.engine.state(), SessionState::Paused);
    const std::size_t delivered = sim.viewer.observer.frame_indices.size();
    sim.run_for(seconds(1));
    EXPECT_EQ(sim.viewer.observer.frame_indices.size(), delivered)
        << "nothing arrives while paused";

    const int keyframes_before = sim.host.observer.keyframe_requests;
    ASSERT_TRUE(sim.viewer.engine.resume_stream(0, sim.network.now).has_value());
    sim.run_for(seconds(1));
    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Streaming);
    EXPECT_GT(sim.host.observer.keyframe_requests, keyframes_before);
    EXPECT_GT(sim.viewer.observer.frame_indices.size(), delivered + 30);
}

TEST(SessionEngine, ByeClosesThePeerCleanly) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(1));
    sim.viewer.engine.close(protocol::ByeReason::UserDisconnected, sim.network.now);
    sim.run_for(milliseconds(50));
    EXPECT_EQ(sim.viewer.engine.state(), SessionState::Closed);
    EXPECT_FALSE(sim.viewer.observer.close_reason.has_value()) << "a local close is not an error";
    EXPECT_EQ(sim.host.observer.close_reason, Error::ConnectionClosed);
}

// --- Cursor
// ----------------------------------------------------------------------------------------

std::vector<std::uint8_t> cursor_image(std::uint16_t width, std::uint16_t height,
                                       std::uint8_t seed) {
    std::vector<std::uint8_t> bgra(std::size_t{width} * height * 4);
    for (std::size_t i = 0; i < bgra.size(); ++i) {
        bgra[i] = static_cast<std::uint8_t>(seed + i * 3);
    }
    return bgra;
}

TEST(SessionEngine, CursorShapesAndPositionsReachTheViewer) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(1));
    ASSERT_EQ(sim.viewer.engine.state(), SessionState::Streaming);

    const auto image = cursor_image(32, 40, 5);
    ASSERT_TRUE(sim.host.engine
                    .push_cursor_shape({.stream_id = 0,
                                        .shape_id = 7,
                                        .width = 32,
                                        .height = 40,
                                        .hotspot_x = 3,
                                        .hotspot_y = 4,
                                        .bgra = image},
                                       sim.network.now)
                    .has_value());
    ASSERT_TRUE(sim.host.engine
                    .push_cursor_position({.stream_id = 0,
                                           .visible = true,
                                           .shape_id = 7,
                                           .x = 100,
                                           .y = -5,
                                           .capture_time_us = 1'000},
                                          sim.network.now)
                    .has_value());
    sim.run_for(milliseconds(500));

    ASSERT_EQ(sim.viewer.observer.cursor_shapes.size(), 1U) << "keyframe resends are not repeated";
    EXPECT_EQ(sim.viewer.observer.cursor_shapes[0].first, 7U);
    EXPECT_EQ(sim.viewer.observer.cursor_shapes[0].second, image);
    ASSERT_EQ(sim.viewer.observer.cursor_positions.size(), 1U);
    EXPECT_EQ(sim.viewer.observer.cursor_positions[0].y, -5);
}

TEST(SessionEngine, OlderCursorPositionsAreDropped) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(1));
    for (const std::uint64_t time : {2'000U, 1'000U, 3'000U}) {
        ASSERT_TRUE(sim.host.engine
                        .push_cursor_position({.stream_id = 0,
                                               .visible = true,
                                               .shape_id = 1,
                                               .x = static_cast<std::int32_t>(time),
                                               .y = 0,
                                               .capture_time_us = time},
                                              sim.network.now)
                        .has_value());
    }
    sim.run_for(milliseconds(100));

    ASSERT_EQ(sim.viewer.observer.cursor_positions.size(), 2U);
    EXPECT_EQ(sim.viewer.observer.cursor_positions[1].x, 3'000);
}

TEST(SessionEngine, ALostCursorShapeReturnsWhenPositionsNameIt) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(1));
    sim.network.media_loss = 1.0;
    const auto image = cursor_image(64, 64, 9);
    ASSERT_TRUE(sim.host.engine
                    .push_cursor_shape({.stream_id = 0,
                                        .shape_id = 2,
                                        .width = 64,
                                        .height = 64,
                                        .hotspot_x = 0,
                                        .hotspot_y = 0,
                                        .bgra = image},
                                       sim.network.now)
                    .has_value());
    sim.run_for(milliseconds(50));
    sim.network.media_loss = 0.0;
    sim.run_for(milliseconds(500));
    ASSERT_TRUE(sim.viewer.observer.cursor_shapes.empty())
        << "frames recovered by NACK, no keyframe";

    // The cursor moves: positions name shape 2, which the viewer lacks, so it asks for a keyframe.
    for (int step = 0; step < 20; ++step) {
        ASSERT_TRUE(sim.host.engine
                        .push_cursor_position({.stream_id = 0,
                                               .visible = true,
                                               .shape_id = 2,
                                               .x = step,
                                               .y = 0,
                                               .capture_time_us = static_cast<std::uint64_t>(step)},
                                              sim.network.now)
                        .has_value());
        sim.run_for(milliseconds(16));
    }
    sim.run_for(milliseconds(300));

    ASSERT_EQ(sim.viewer.observer.cursor_shapes.size(), 1U);
    EXPECT_EQ(sim.viewer.observer.cursor_shapes[0].second, image);
}

TEST(SessionEngine, InvalidCursorShapesAreRejected) {
    Simulation sim;
    sim.connect();
    sim.run_for(seconds(1));
    const auto image = cursor_image(8, 8, 0);
    EXPECT_EQ(sim.host.engine
                  .push_cursor_shape({.stream_id = 0,
                                      .shape_id = 1,
                                      .width = 8,
                                      .height = 8,
                                      .hotspot_x = 8,
                                      .hotspot_y = 0,
                                      .bgra = image},
                                     sim.network.now)
                  .error(),
              Error::InvalidArgument);
    EXPECT_EQ(sim.host.engine
                  .push_cursor_shape({.stream_id = 3,
                                      .shape_id = 1,
                                      .width = 8,
                                      .height = 8,
                                      .hotspot_x = 0,
                                      .hotspot_y = 0,
                                      .bgra = image},
                                     sim.network.now)
                  .error(),
              Error::InvalidState);
}

} // namespace
