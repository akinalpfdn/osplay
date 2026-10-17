#include "protocol/byte_io.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/media_packets.hpp"
#include "protocol/text.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace {

namespace protocol = osplay::protocol;
using protocol::ControlMessage;
using protocol::Error;
using protocol::MediaPacket;

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

std::vector<ControlMessage> boundary_messages() {
    std::vector<protocol::Monitor> monitors;
    for (std::uint8_t id = 0; id < protocol::kMaxMonitors; ++id) {
        monitors.push_back({.monitor_id = static_cast<std::uint8_t>(id * 60),
                            .width_px = id == 0 ? std::uint16_t{320} : std::uint16_t{16384},
                            .height_px = id == 0 ? std::uint16_t{240} : std::uint16_t{16384},
                            .refresh_millihertz = id == 0 ? 1000U : 500'000U,
                            .physical_width_mm = 10'000,
                            .physical_height_mm = 0,
                            .scale_percent = id == 0 ? std::uint16_t{100} : std::uint16_t{400},
                            .name = std::string(protocol::kMaxStringBytes, 'm')});
    }
    std::vector<protocol::NackRange> ranges(protocol::kMaxNackRanges,
                                            {.first_fragment = 4095, .count = 1});
    protocol::SessionToken token{};
    token.fill(0xFF);
    return {
        protocol::Hello{.role = protocol::Role::Viewer,
                        .platform = protocol::Platform::Windows,
                        .protocol_minor = 255,
                        .device_name = std::string(protocol::kMaxStringBytes, 'x')},
        protocol::HandshakeMessage{
            .step = 3, .payload = std::vector<std::uint8_t>(protocol::kMaxHandshakePayload, 7)},
        protocol::PairingConfirm{.decision = protocol::PairingDecision::Rejected},
        protocol::Capabilities{.codecs = protocol::kCodecH264AnnexB,
                               .max_streams = 4,
                               .flags = 0,
                               .max_width = 320,
                               .max_height = 240,
                               .max_refresh_hz = 24},
        protocol::ViewerMonitors{.monitors = monitors},
        protocol::StreamConfig{.stream_id = 3,
                               .monitor_id = 255,
                               .width = 8192,
                               .height = 8192,
                               .refresh_millihertz = 240'000,
                               .codec = protocol::Codec::H264,
                               .profile = protocol::H264Profile::Baseline,
                               .cursor_mode = protocol::CursorMode::Separate,
                               .bitrate_kbps = 1'000'000},
        protocol::StreamStart{.stream_id = 3},
        protocol::StreamPause{.stream_id = 2},
        protocol::StreamResume{.stream_id = 1},
        protocol::StreamStop{.stream_id = 0},
        protocol::KeyframeRequest{.stream_id = 3, .reason = protocol::KeyframeReason::StreamResume},
        protocol::Nack{.stream_id = 2, .frame_id = 0xFFFF'FFFF, .ranges = ranges},
        protocol::ReceiverReport{.stream_id = 3,
                                 .loss_permille = 1000,
                                 .frames_received = 0xFFFF'FFFF,
                                 .frames_dropped = 0,
                                 .decode_time_us = 1,
                                 .jitter_us = 0xFFFF'FFFF},
        protocol::Ping{.ping_id = 0, .send_time_us = 0xFFFF'FFFF'FFFF'FFFF},
        protocol::Pong{.ping_id = 0xFFFF'FFFF,
                       .ping_send_time_us = 1,
                       .receive_time_us = 2,
                       .reply_time_us = 3},
        protocol::Heartbeat{},
        protocol::PeerError{.code = 0xFFFF},
        protocol::Bye{.reason = protocol::ByeReason::Error},
        protocol::MediaSetup{.udp_port = 65535, .session_token = token},
    };
}

TEST(ControlCodec, BoundaryValuesOfEveryTypeRoundTrip) {
    const auto messages = boundary_messages();
    ASSERT_EQ(messages.size(), std::variant_size_v<ControlMessage>);
    for (const ControlMessage& message : messages) {
        SCOPED_TRACE(static_cast<int>(protocol::type_of(message)));
        const auto encoded = protocol::encode_control(message);
        ASSERT_TRUE(encoded.has_value());
        ASSERT_LE(encoded.value().size(), protocol::kMaxControlMessageSize);
        const auto decoded = protocol::decode_control(encoded.value());
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded.value(), message);
    }
}

TEST(ControlCodec, EncoderRefusesWhatTheDecoderWouldReject) {
    const auto error = [](const ControlMessage& message) {
        const auto encoded = protocol::encode_control(message);
        EXPECT_FALSE(encoded.has_value());
        return encoded.has_value() ? Error::InvalidArgument : encoded.error();
    };
    EXPECT_EQ(error(protocol::Hello{.device_name = ""}), Error::InvalidString);
    EXPECT_EQ(error(protocol::Hello{.device_name = std::string(65, 'x')}), Error::InvalidString);
    EXPECT_EQ(error(protocol::Hello{.role = static_cast<protocol::Role>(0), .device_name = "PC"}),
              Error::ValueOutOfRange);
    EXPECT_EQ(
        error(protocol::HandshakeMessage{.step = 1, .payload = std::vector<std::uint8_t>(1025)}),
        Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::ViewerMonitors{.monitors = std::vector<protocol::Monitor>(5)}),
              Error::ValueOutOfRange);
    EXPECT_EQ(
        error(protocol::StreamConfig{
            .width = 1921, .height = 1080, .refresh_millihertz = 60'000, .bitrate_kbps = 1000}),
        Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::Nack{.ranges = std::vector<protocol::NackRange>(
                                       33, {.first_fragment = 0, .count = 1})}),
              Error::ValueOutOfRange);
    EXPECT_EQ(error(protocol::MediaSetup{.udp_port = 0}), Error::ValueOutOfRange);
}

TEST(MediaCodec, FramesSplitIntoFullFragmentsRoundTrip) {
    std::vector<std::uint8_t> frame(3000);
    for (std::size_t i = 0; i < frame.size(); ++i) {
        frame[i] = static_cast<std::uint8_t>(i * 7);
    }
    const auto count = static_cast<std::uint16_t>((frame.size() + protocol::kMaxVideoPayload - 1) /
                                                  protocol::kMaxVideoPayload);
    ASSERT_EQ(count, 3);

    std::vector<std::uint8_t> reassembled;
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::size_t offset = std::size_t{index} * protocol::kMaxVideoPayload;
        const std::size_t length = std::min(protocol::kMaxVideoPayload, frame.size() - offset);
        const MediaPacket packet{
            .sequence = 100U + index,
            .body = protocol::VideoFragment{.stream_id = 1,
                                            .flags = protocol::kVideoKeyframe,
                                            .frame_id = 9,
                                            .fragment_index = index,
                                            .fragment_count = count,
                                            .capture_time_us = 42,
                                            .payload = std::span(frame).subspan(offset, length)}};
        std::array<std::uint8_t, protocol::kMaxMediaPlaintextSize> buffer{};
        const auto written = protocol::encode_media(packet, buffer);
        ASSERT_TRUE(written.has_value());
        ASSERT_LE(written.value() + protocol::kAeadTagSize, protocol::kMaxDatagramSize);
        const auto decoded = protocol::decode_media(std::span(buffer).first(written.value()));
        ASSERT_TRUE(decoded.has_value());
        ASSERT_EQ(decoded.value(), packet);
        const auto& fragment = std::get<protocol::VideoFragment>(decoded.value().body);
        reassembled.insert(reassembled.end(), fragment.payload.begin(), fragment.payload.end());
    }
    EXPECT_EQ(reassembled, frame);
}

TEST(MediaCodec, EncoderChecksBufferAndValues) {
    const std::array<std::uint8_t, 10> payload{};
    const MediaPacket packet{.sequence = 1,
                             .body = protocol::VideoFragment{
                                 .fragment_index = 0, .fragment_count = 1, .payload = payload}};
    std::array<std::uint8_t, 20> small{};
    const auto too_small = protocol::encode_media(packet, small);
    ASSERT_FALSE(too_small.has_value());
    EXPECT_EQ(too_small.error(), Error::BufferTooSmall);

    const MediaPacket short_middle{
        .sequence = 1,
        .body =
            protocol::VideoFragment{.fragment_index = 0, .fragment_count = 2, .payload = payload}};
    std::array<std::uint8_t, protocol::kMaxMediaPlaintextSize> buffer{};
    const auto refused = protocol::encode_media(short_middle, buffer);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), Error::ValueOutOfRange);
}

TEST(MediaCodec, CursorPositionAndUdpHelloRoundTrip) {
    protocol::SessionToken token{};
    token.fill(3);
    for (const MediaPacket& packet : {
             MediaPacket{.sequence = 0xFFFF'FFFF'FFFF'FFFF,
                         .body = protocol::UdpHello{.session_token = token}},
             MediaPacket{.sequence = 5,
                         .body = protocol::CursorPosition{.stream_id = 3,
                                                          .visible = false,
                                                          .shape_id = 1,
                                                          .x = -2'147'483'647 - 1,
                                                          .y = 2'147'483'647,
                                                          .capture_time_us = 9}},
         }) {
        std::array<std::uint8_t, protocol::kMaxMediaPlaintextSize> buffer{};
        const auto written = protocol::encode_media(packet, buffer);
        ASSERT_TRUE(written.has_value());
        const auto decoded = protocol::decode_media(std::span(buffer).first(written.value()));
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded.value(), packet);
    }
}

TEST(MediaCodec, CursorShapeFragmentSizes) {
    EXPECT_EQ(protocol::cursor_shape_fragment_count(1, 1), 1);
    EXPECT_EQ(protocol::cursor_shape_fragment_size(1, 1, 0), 4U);
    EXPECT_EQ(protocol::cursor_shape_fragment_count(256, 256), 227);
    EXPECT_EQ(protocol::cursor_shape_fragment_size(256, 256, 0), protocol::kMaxCursorShapePayload);
    EXPECT_EQ(protocol::cursor_shape_fragment_size(256, 256, 226),
              256U * 256U * 4U - 226U * protocol::kMaxCursorShapePayload);
    EXPECT_EQ(protocol::cursor_shape_fragment_size(256, 256, 227), 0U);
}

TEST(ProtocolText, AcceptsUtf8AndRejectsControlsAndMalformedSequences) {
    EXPECT_TRUE(protocol::is_valid_text(bytes_of("")));
    EXPECT_TRUE(protocol::is_valid_text(bytes_of("Çalışma 💻 日本")));
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("tab\tseparated")));
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\x7F")));
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xC2\x85")));         // U+0085, C1 control
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xE2\x82")));         // truncated
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xF4\x90\x80\x80"))); // above U+10FFFF
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xED\xB0\x80")));     // surrogate
    EXPECT_FALSE(protocol::is_valid_text(bytes_of("\xE0\x80\xAF")));     // overlong
}

TEST(ByteIo, WriterStopsAtTheEndAndReaderNeverReadsPastIt) {
    std::array<std::uint8_t, 3> out{};
    protocol::Writer writer(out);
    writer.u16(0x0201);
    EXPECT_TRUE(writer.ok());
    writer.u16(0x0403);
    EXPECT_FALSE(writer.ok());
    EXPECT_EQ(writer.size(), 2U);

    protocol::Reader reader(out);
    std::uint16_t value = 0;
    std::uint16_t second = 0;
    EXPECT_TRUE(reader.u16(value));
    EXPECT_EQ(value, 0x0201);
    EXPECT_FALSE(reader.u16(second));
    EXPECT_EQ(reader.remaining(), 1U);
}

} // namespace
