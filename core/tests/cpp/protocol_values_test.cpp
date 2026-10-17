// Cross-checks the C++ decoder against protocol/tools/generate_fixtures.py: each valid fixture,
// written by the Python encoder from docs/PROTOCOL.md, must decode to the field values the
// generator used.
#include "fixtures.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/media_packets.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

namespace {

namespace protocol = osplay::protocol;
using protocol::ControlMessage;

ControlMessage control(const std::string& file) {
    const auto decoded = protocol::decode_control(osplay::test::read_fixture("control/" + file));
    if (!decoded) {
        ADD_FAILURE() << file << " failed with error " << static_cast<int>(decoded.error());
        return protocol::Heartbeat{};
    }
    return decoded.value();
}

std::vector<std::uint8_t> sequence(std::size_t count, std::size_t start = 0,
                                   std::size_t modulo = 256) {
    std::vector<std::uint8_t> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<std::uint8_t>((start + i) % modulo);
    }
    return bytes;
}

protocol::SessionToken token() {
    protocol::SessionToken value{};
    std::iota(value.begin(), value.end(), std::uint8_t{0xA0});
    return value;
}

TEST(ProtocolValues, Hello) {
    EXPECT_EQ(control("hello_viewer.bin"),
              ControlMessage(protocol::Hello{.role = protocol::Role::Viewer,
                                             .platform = protocol::Platform::Windows,
                                             .protocol_minor = 0,
                                             .device_name = "Akinalp's PC"}));
    EXPECT_EQ(control("hello_host_utf8.bin"),
              ControlMessage(protocol::Hello{.role = protocol::Role::Host,
                                             .platform = protocol::Platform::MacOS,
                                             .protocol_minor = 0,
                                             .device_name = "MacBook Air — Çalışma"}));
}

TEST(ProtocolValues, HandshakeAndPairing) {
    EXPECT_EQ(control("handshake_step1.bin"),
              ControlMessage(protocol::HandshakeMessage{.step = 1, .payload = sequence(32)}));
    EXPECT_EQ(control("pairing_accept.bin"), ControlMessage(protocol::PairingConfirm{
                                                 .decision = protocol::PairingDecision::Accepted}));
}

TEST(ProtocolValues, CapabilitiesAndMonitors) {
    EXPECT_EQ(control("capabilities.bin"),
              ControlMessage(protocol::Capabilities{.codecs = 1,
                                                    .max_streams = 2,
                                                    .flags = 0b11,
                                                    .max_width = 7680,
                                                    .max_height = 4320,
                                                    .max_refresh_hz = 240}));
    EXPECT_EQ(control("viewer_monitors_two.bin"),
              ControlMessage(protocol::ViewerMonitors{.monitors = {
                                                          {.monitor_id = 0,
                                                           .width_px = 2560,
                                                           .height_px = 1440,
                                                           .refresh_millihertz = 60'000,
                                                           .physical_width_mm = 597,
                                                           .physical_height_mm = 336,
                                                           .scale_percent = 100,
                                                           .name = "DELL U2723QE"},
                                                          {.monitor_id = 1,
                                                           .width_px = 1920,
                                                           .height_px = 1080,
                                                           .refresh_millihertz = 144'000,
                                                           .physical_width_mm = 527,
                                                           .physical_height_mm = 296,
                                                           .scale_percent = 100,
                                                           .name = ""},
                                                      }}));
}

TEST(ProtocolValues, StreamControl) {
    EXPECT_EQ(control("stream_config.bin"), ControlMessage(protocol::StreamConfig{
                                                .stream_id = 0,
                                                .monitor_id = 0,
                                                .width = 2560,
                                                .height = 1440,
                                                .refresh_millihertz = 60'000,
                                                .codec = protocol::Codec::H264,
                                                .profile = protocol::H264Profile::High,
                                                .cursor_mode = protocol::CursorMode::InVideo,
                                                .bitrate_kbps = 30'000,
                                            }));
    EXPECT_EQ(control("stream_start.bin"), ControlMessage(protocol::StreamStart{.stream_id = 1}));
    EXPECT_EQ(control("stream_pause.bin"), ControlMessage(protocol::StreamPause{.stream_id = 0}));
    EXPECT_EQ(control("stream_resume.bin"), ControlMessage(protocol::StreamResume{.stream_id = 0}));
    EXPECT_EQ(control("stream_stop.bin"), ControlMessage(protocol::StreamStop{.stream_id = 3}));
    EXPECT_EQ(control("keyframe_request.bin"),
              ControlMessage(protocol::KeyframeRequest{
                  .stream_id = 0, .reason = protocol::KeyframeReason::FrameLost}));
    EXPECT_EQ(control("nack.bin"),
              ControlMessage(protocol::Nack{.stream_id = 0,
                                            .frame_id = 123'456,
                                            .ranges = {{.first_fragment = 3, .count = 2},
                                                       {.first_fragment = 10, .count = 1}}}));
    EXPECT_EQ(control("receiver_report.bin"),
              ControlMessage(protocol::ReceiverReport{.stream_id = 1,
                                                      .loss_permille = 12,
                                                      .frames_received = 36'000,
                                                      .frames_dropped = 17,
                                                      .decode_time_us = 2'350,
                                                      .jitter_us = 410}));
}

TEST(ProtocolValues, TimingAndSessionMessages) {
    EXPECT_EQ(control("ping.bin"),
              ControlMessage(protocol::Ping{.ping_id = 77, .send_time_us = 1'000'000}));
    EXPECT_EQ(control("pong.bin"), ControlMessage(protocol::Pong{.ping_id = 77,
                                                                 .ping_send_time_us = 1'000'000,
                                                                 .receive_time_us = 9'000'500,
                                                                 .reply_time_us = 9'000'620}));
    EXPECT_EQ(control("heartbeat.bin"), ControlMessage(protocol::Heartbeat{}));
    EXPECT_EQ(control("error_incompatible.bin"), ControlMessage(protocol::PeerError{.code = 1}));
    EXPECT_EQ(control("error_unknown_code.bin"), ControlMessage(protocol::PeerError{.code = 999}));
    EXPECT_EQ(control("bye.bin"),
              ControlMessage(protocol::Bye{.reason = protocol::ByeReason::Sleeping}));
    EXPECT_EQ(control("media_setup.bin"),
              ControlMessage(protocol::MediaSetup{.udp_port = 50'123, .session_token = token()}));
}

TEST(ProtocolValues, MediaPackets) {
    const auto media = [](const std::string& file, const protocol::MediaPacket& expected) {
        const auto bytes = osplay::test::read_fixture("media/" + file);
        const auto decoded = protocol::decode_media(bytes);
        ASSERT_TRUE(decoded.has_value()) << file;
        EXPECT_EQ(decoded.value(), expected) << file;
    };

    media("udp_hello.bin", {.sequence = 0, .body = protocol::UdpHello{.session_token = token()}});

    std::vector<std::uint8_t> keyframe;
    for (const char* hex :
         {"000000016764001FACD940501EC8", "0000000168EBE3CB22C0", "0000000165888400"}) {
        for (std::size_t i = 0; hex[i] != '\0'; i += 2) {
            keyframe.push_back(
                static_cast<std::uint8_t>(std::stoi(std::string(hex + i, 2), nullptr, 16)));
        }
    }
    const auto idr_tail = sequence(32);
    keyframe.insert(keyframe.end(), idr_tail.begin(), idr_tail.end());
    media("video_keyframe_single.bin",
          {.sequence = 7,
           .body = protocol::VideoFragment{.stream_id = 0,
                                           .flags = 0b11,
                                           .frame_id = 1,
                                           .fragment_index = 0,
                                           .fragment_count = 1,
                                           .capture_time_us = 5'000'123'456,
                                           .payload = keyframe}});

    const auto full = sequence(protocol::kMaxVideoPayload, 0, 251);
    media("video_middle_fragment.bin",
          {.sequence = 8,
           .body = protocol::VideoFragment{.stream_id = 0,
                                           .flags = 0,
                                           .frame_id = 2,
                                           .fragment_index = 1,
                                           .fragment_count = 3,
                                           .capture_time_us = 5'000'123'456,
                                           .payload = full}});
    media("video_last_fragment.bin",
          {.sequence = 9,
           .body = protocol::VideoFragment{.stream_id = 0,
                                           .flags = 0,
                                           .frame_id = 2,
                                           .fragment_index = 2,
                                           .fragment_count = 3,
                                           .capture_time_us = 5'000'123'456,
                                           .payload = std::span(full).first(100)}});

    media("cursor_position.bin",
          {.sequence = 9,
           .body = protocol::CursorPosition{.stream_id = 0,
                                            .visible = true,
                                            .shape_id = 42,
                                            .x = -5,
                                            .y = 1200,
                                            .capture_time_us = 5'000'200'000}});

    const auto shape16 = sequence(16 * 16 * 4);
    media("cursor_shape_single.bin", {.sequence = 11,
                                      .body = protocol::CursorShapeFragment{.stream_id = 0,
                                                                            .shape_id = 7,
                                                                            .width = 16,
                                                                            .height = 16,
                                                                            .hotspot_x = 1,
                                                                            .hotspot_y = 2,
                                                                            .fragment_index = 0,
                                                                            .fragment_count = 1,
                                                                            .payload = shape16}});
    const auto first = sequence(protocol::kMaxCursorShapePayload);
    media("cursor_shape_first.bin", {.sequence = 11,
                                     .body = protocol::CursorShapeFragment{.stream_id = 0,
                                                                           .shape_id = 7,
                                                                           .width = 32,
                                                                           .height = 32,
                                                                           .hotspot_x = 1,
                                                                           .hotspot_y = 2,
                                                                           .fragment_index = 0,
                                                                           .fragment_count = 4,
                                                                           .payload = first}});
    const auto last = sequence(32 * 32 * 4 - 3 * protocol::kMaxCursorShapePayload,
                               3 * protocol::kMaxCursorShapePayload);
    media("cursor_shape_last.bin", {.sequence = 11,
                                    .body = protocol::CursorShapeFragment{.stream_id = 0,
                                                                          .shape_id = 7,
                                                                          .width = 32,
                                                                          .height = 32,
                                                                          .hotspot_x = 1,
                                                                          .hotspot_y = 2,
                                                                          .fragment_index = 3,
                                                                          .fragment_count = 4,
                                                                          .payload = last}});
}

} // namespace
