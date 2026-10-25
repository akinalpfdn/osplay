#include <osplay/win/helper_protocol.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using osplay::win::helper::Bytes;
using osplay::win::helper::Command;
using osplay::win::helper::DecodeError;
using osplay::win::helper::MonitorConfig;
using osplay::win::helper::Request;
using osplay::win::helper::Response;
using osplay::win::helper::ResultCode;
namespace helper = osplay::win::helper;

MonitorConfig valid_config() {
    return MonitorConfig{
        .width = 2560, .height = 1664, .refresh_millihertz = 60'000, .gpu_name = ""};
}

Request configure(MonitorConfig config) {
    return Request{.command = Command::Configure, .monitors = {std::move(config)}};
}

DecodeError request_error(const Bytes& bytes) {
    const auto decoded = helper::decode_request(bytes);
    EXPECT_FALSE(decoded.has_value());
    return decoded.has_value() ? DecodeError::Truncated : decoded.error();
}

TEST(HelperProtocol, StatusRequestHasPinnedByteLayout) {
    const Bytes expected{'O', 'S', 'P', 'H', 0x03, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(helper::encode(Request{.command = Command::Status, .monitors = {}}), expected);
}

TEST(HelperProtocol, EnableRequestNamesItsDevices) {
    // Header with a one-byte payload: devices 0 and 2.
    const Bytes expected{'O', 'S', 'P', 'H', 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05};
    EXPECT_EQ(helper::encode(Request{.command = Command::Enable, .monitors = {}, .devices = 0x05}),
              expected);
}

TEST(HelperProtocol, EnableAndDisableNeedOneToAllDevices) {
    for (const Command command : {Command::Enable, Command::Disable}) {
        Bytes none = helper::encode(Request{.command = command, .monitors = {}, .devices = 1});
        none[12] = 0;
        EXPECT_EQ(request_error(none), DecodeError::ValueOutOfRange);
        Bytes beyond = helper::encode(Request{.command = command, .monitors = {}, .devices = 1});
        beyond[12] = helper::kAllDevices + 1;
        EXPECT_EQ(request_error(beyond), DecodeError::ValueOutOfRange);
    }
}

TEST(HelperProtocol, ConfigureRequestHasPinnedByteLayout) {
    MonitorConfig config = valid_config();
    config.gpu_name = "GPU";
    // Header, then count 1, reserved, GPU name length 3, "GPU", then width, height, refresh.
    const Bytes expected{'O',  'S',  'P',  'H',  0x03, 0x00, 0x01, 0x00, 0x13, 0x00, 0x00,
                         0x00, 0x01, 0x00, 0x03, 0x00, 'G',  'P',  'U',  0x00, 0x0A, 0x00,
                         0x00, 0x80, 0x06, 0x00, 0x00, 0x60, 0xEA, 0x00, 0x00};
    EXPECT_EQ(helper::encode(configure(config)), expected);
}

TEST(HelperProtocol, SeveralMonitorsRoundTripInOrder) {
    MonitorConfig second{
        .width = 1920, .height = 1080, .refresh_millihertz = 59'940, .gpu_name = ""};
    const Request request{.command = Command::Configure,
                          .monitors = {valid_config(), second, valid_config(), second}};
    const auto decoded = helper::decode_request(helper::encode(request));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), request);
}

TEST(HelperProtocol, ConfigureNeedsOneToFourMonitorsOnOneGpu) {
    EXPECT_FALSE(helper::is_valid(std::vector<MonitorConfig>{}));
    EXPECT_FALSE(
        helper::is_valid(std::vector<MonitorConfig>(helper::kMaxMonitors + 1, valid_config())));
    EXPECT_TRUE(helper::is_valid(std::vector<MonitorConfig>(helper::kMaxMonitors, valid_config())));
    MonitorConfig other_gpu = valid_config();
    other_gpu.gpu_name = "Other";
    EXPECT_FALSE(helper::is_valid(std::vector<MonitorConfig>{valid_config(), other_gpu}));

    Bytes none = helper::encode(configure(valid_config()));
    none[12] = 0; // monitor count
    EXPECT_EQ(request_error(none), DecodeError::ValueOutOfRange);
    Bytes five = helper::encode(configure(valid_config()));
    five[12] = 5;
    EXPECT_EQ(request_error(five), DecodeError::ValueOutOfRange);
}

TEST(HelperProtocol, EveryRequestRoundTrips) {
    MonitorConfig named = valid_config();
    named.gpu_name = "NVIDIA GeForce RTX 4070 — Çalışma";
    const std::vector<Request> requests{
        configure(valid_config()),
        configure(named),
        Request{.command = Command::Enable, .monitors = {}, .devices = 0x01},
        Request{.command = Command::Disable, .monitors = {}, .devices = helper::kAllDevices},
        Request{.command = Command::Status, .monitors = {}},
    };
    for (const Request& request : requests) {
        const auto decoded = helper::decode_request(helper::encode(request));
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(decoded.value(), request);
    }
}

TEST(HelperProtocol, ResponseRoundTrips) {
    const Response response{
        .result = ResultCode::Ok,
        .status = {.vdd_installed = true,
                   .enabled_devices = 0x05,
                   .configured = true,
                   .monitor_count = 2,
                   .width = 2560,
                   .height = 1664,
                   .refresh_millihertz = 60'000},
    };
    const auto decoded = helper::decode_response(helper::encode(response));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value(), response);
}

TEST(HelperProtocol, RejectsTruncatedHeader) {
    Bytes bytes = helper::encode(Request{.command = Command::Status, .monitors = {}});
    bytes.resize(helper::kHeaderSize - 1);
    EXPECT_EQ(request_error(bytes), DecodeError::Truncated);
    EXPECT_EQ(request_error({}), DecodeError::Truncated);
}

TEST(HelperProtocol, RejectsBadMagic) {
    Bytes bytes = helper::encode(Request{.command = Command::Status, .monitors = {}});
    bytes[0] = 'X';
    EXPECT_EQ(request_error(bytes), DecodeError::BadMagic);
}

TEST(HelperProtocol, RejectsOtherVersions) {
    Bytes bytes = helper::encode(Request{.command = Command::Status, .monitors = {}});
    bytes[4] = 2; // a helper or engine from before devices were named one by one
    EXPECT_EQ(request_error(bytes), DecodeError::UnsupportedVersion);
}

TEST(HelperProtocol, RejectsUnknownCommands) {
    Bytes bytes = helper::encode(Request{.command = Command::Status, .monitors = {}});
    bytes[6] = 0x7F;
    EXPECT_EQ(request_error(bytes), DecodeError::UnknownCommand);
    bytes[6] = 0x00;
    EXPECT_EQ(request_error(bytes), DecodeError::UnknownCommand);
}

TEST(HelperProtocol, RejectsPayloadLengthMismatch) {
    Bytes bytes = helper::encode(Request{.command = Command::Status, .monitors = {}});
    bytes.push_back(0);
    EXPECT_EQ(request_error(bytes), DecodeError::LengthMismatch);

    Bytes long_enable =
        helper::encode(Request{.command = Command::Enable, .monitors = {}, .devices = 1});
    long_enable[8] = 2;
    long_enable.push_back(0);
    EXPECT_EQ(request_error(long_enable), DecodeError::LengthMismatch);
    Bytes empty_disable =
        helper::encode(Request{.command = Command::Disable, .monitors = {}, .devices = 1});
    empty_disable[8] = 0;
    empty_disable.pop_back();
    EXPECT_EQ(request_error(empty_disable), DecodeError::LengthMismatch);

    Bytes configure_bytes = helper::encode(configure(valid_config()));
    configure_bytes[14] = 5; // GPU name length larger than what follows
    EXPECT_EQ(request_error(configure_bytes), DecodeError::LengthMismatch);
}

TEST(HelperProtocol, RejectsShortConfigurePayload) {
    Bytes bytes = helper::encode(configure(valid_config()));
    bytes.resize(bytes.size() - 2);
    bytes[8] = static_cast<std::uint8_t>(bytes.size() - helper::kHeaderSize);
    EXPECT_EQ(request_error(bytes), DecodeError::LengthMismatch) << "a mode cut short";

    Bytes header_only = helper::encode(configure(valid_config()));
    header_only.resize(helper::kHeaderSize + 2);
    header_only[8] = 2;
    EXPECT_EQ(request_error(header_only), DecodeError::Truncated);
}

TEST(HelperProtocol, RejectsOversizedMessages) {
    Bytes bytes(helper::kMaxMessageSize + 1, 0);
    EXPECT_EQ(request_error(bytes), DecodeError::TooLarge);
}

TEST(HelperProtocol, RejectsOutOfRangeMonitorValues) {
    const std::vector<MonitorConfig> invalid{
        {.width = 639, .height = 1080, .refresh_millihertz = 60'000, .gpu_name = ""},
        {.width = 7681, .height = 1080, .refresh_millihertz = 60'000, .gpu_name = ""},
        {.width = 1920, .height = 479, .refresh_millihertz = 60'000, .gpu_name = ""},
        {.width = 1920, .height = 4321, .refresh_millihertz = 60'000, .gpu_name = ""},
        {.width = 1920, .height = 1080, .refresh_millihertz = 23'999, .gpu_name = ""},
        {.width = 1920, .height = 1080, .refresh_millihertz = 240'001, .gpu_name = ""},
    };
    for (const MonitorConfig& config : invalid) {
        EXPECT_FALSE(helper::is_valid(config));
        EXPECT_EQ(request_error(helper::encode(configure(config))), DecodeError::ValueOutOfRange);
    }
}

TEST(HelperProtocol, AcceptsBoundaryValues) {
    EXPECT_TRUE(helper::is_valid(
        {.width = 640, .height = 480, .refresh_millihertz = 24'000, .gpu_name = ""}));
    EXPECT_TRUE(helper::is_valid(
        {.width = 7680, .height = 4320, .refresh_millihertz = 240'000, .gpu_name = ""}));
}

TEST(HelperProtocol, RejectsUnsafeGpuNames) {
    const std::vector<std::string> invalid{
        std::string("line\nbreak"),
        std::string("nul\0byte", 8),
        std::string("\xC3"),         // truncated sequence
        std::string("\xC0\xAF"),     // overlong encoding
        std::string("\xED\xA0\x80"), // surrogate
        std::string("\xFF"),         // invalid lead byte
        std::string(helper::kMaxGpuNameBytes + 1, 'a'),
    };
    for (const std::string& name : invalid) {
        MonitorConfig config = valid_config();
        config.gpu_name = name;
        EXPECT_FALSE(helper::is_valid(config));
        EXPECT_EQ(request_error(helper::encode(configure(config))), DecodeError::InvalidGpuName);
    }
}

TEST(HelperProtocol, RejectsResponsesWithUnknownCodesOrBadLength) {
    Bytes bytes = helper::encode(Response{});
    bytes[6] = 0x40;
    EXPECT_FALSE(helper::decode_response(bytes).has_value());

    Bytes truncated = helper::encode(Response{});
    truncated.pop_back();
    truncated[8] = static_cast<std::uint8_t>(truncated.size() - helper::kHeaderSize);
    ASSERT_FALSE(helper::decode_response(truncated).has_value());
    EXPECT_EQ(helper::decode_response(truncated).error(), DecodeError::LengthMismatch);
}

TEST(HelperProtocol, MapsDecodeErrorsToResultCodes) {
    EXPECT_EQ(helper::result_for(DecodeError::UnsupportedVersion), ResultCode::UnsupportedVersion);
    EXPECT_EQ(helper::result_for(DecodeError::UnknownCommand), ResultCode::UnknownCommand);
    EXPECT_EQ(helper::result_for(DecodeError::ValueOutOfRange), ResultCode::ValueOutOfRange);
    EXPECT_EQ(helper::result_for(DecodeError::InvalidGpuName), ResultCode::ValueOutOfRange);
    EXPECT_EQ(helper::result_for(DecodeError::BadMagic), ResultCode::MalformedRequest);
    EXPECT_EQ(helper::result_for(DecodeError::Truncated), ResultCode::MalformedRequest);
}

} // namespace
