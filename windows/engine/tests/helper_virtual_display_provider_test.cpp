#include "../src/display/helper_virtual_display_provider.hpp"

#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace {

using osplay::engine::DisplayError;
using osplay::engine::DisplayModeApplier;
using osplay::engine::HelperChannel;
using osplay::engine::HelperVirtualDisplayProvider;
using osplay::engine::VirtualMonitorConfig;
namespace helper = osplay::win::helper;
namespace win = osplay::win;

class FakeChannel final : public HelperChannel {
public:
    std::vector<helper::Request> requests;
    std::deque<win::Expected<helper::Bytes, DisplayError>> replies;

    win::Expected<helper::Bytes, DisplayError>
    transact(std::span<const std::uint8_t> request) override {
        const auto decoded = helper::decode_request(request);
        EXPECT_TRUE(decoded.has_value());
        if (decoded) {
            requests.push_back(decoded.value());
        }
        auto reply = replies.front();
        replies.pop_front();
        return reply;
    }

    void reply(helper::ResultCode code, helper::MonitorStatus status = {}) {
        replies.emplace_back(helper::encode(helper::Response{.result = code, .status = status}));
    }
};

class FakeModeApplier final : public DisplayModeApplier {
public:
    std::vector<VirtualMonitorConfig> applied;
    std::optional<DisplayError> error;

    win::Expected<std::vector<std::wstring>, DisplayError>
    apply(std::span<const VirtualMonitorConfig> configs) override {
        applied.assign(configs.begin(), configs.end());
        if (error) {
            return win::Expected<std::vector<std::wstring>, DisplayError>::failure(*error);
        }
        std::vector<std::wstring> names;
        for (std::size_t i = 0; i < configs.size(); ++i) {
            names.push_back(L"DISPLAY" + std::to_wstring(i));
        }
        return names;
    }
};

struct Fixture {
    FakeChannel* channel = nullptr;
    FakeModeApplier* applier = nullptr;
    std::unique_ptr<HelperVirtualDisplayProvider> provider;

    Fixture() {
        auto owned_channel = std::make_unique<FakeChannel>();
        auto owned_applier = std::make_unique<FakeModeApplier>();
        channel = owned_channel.get();
        applier = owned_applier.get();
        provider = std::make_unique<HelperVirtualDisplayProvider>(std::move(owned_channel),
                                                                  std::move(owned_applier));
    }
};

VirtualMonitorConfig config() {
    return {.width = 2560, .height = 1664, .refresh_millihertz = 60'000, .gpu_name = ""};
}

TEST(HelperVirtualDisplayProvider, CreateConfiguresThenEnables) {
    Fixture f;
    f.channel->reply(helper::ResultCode::Ok);
    f.channel->reply(helper::ResultCode::Ok);

    EXPECT_TRUE(f.provider->create_monitors(std::vector{config()}).has_value());

    ASSERT_EQ(f.channel->requests.size(), 2U);
    EXPECT_EQ(f.channel->requests[0].command, helper::Command::Configure);
    EXPECT_EQ(f.channel->requests[0].monitors, std::vector{config()});
    EXPECT_EQ(f.channel->requests[1].command, helper::Command::Enable);
    ASSERT_EQ(f.applier->applied.size(), 1U);
    EXPECT_EQ(f.applier->applied[0], config());
}

TEST(HelperVirtualDisplayProvider, ModeFailureRemovesTheMonitor) {
    Fixture f;
    f.applier->error = DisplayError::ModeChangeFailed;
    f.channel->reply(helper::ResultCode::Ok); // configure
    f.channel->reply(helper::ResultCode::Ok); // enable
    f.channel->reply(helper::ResultCode::Ok); // disable

    const auto result = f.provider->create_monitors(std::vector{config()});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DisplayError::ModeChangeFailed);
    ASSERT_EQ(f.channel->requests.size(), 3U);
    EXPECT_EQ(f.channel->requests[2].command, helper::Command::Disable);
}

TEST(HelperVirtualDisplayProvider, EnableFailureSkipsTheModeChange) {
    Fixture f;
    f.channel->reply(helper::ResultCode::Ok);
    f.channel->reply(helper::ResultCode::DeviceControlFailed);

    const auto result = f.provider->create_monitors(std::vector{config()});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DisplayError::DeviceControlFailed);
    EXPECT_TRUE(f.applier->applied.empty());
}

TEST(HelperVirtualDisplayProvider, InvalidConfigNeverReachesTheHelper) {
    Fixture f;
    VirtualMonitorConfig bad = config();
    bad.refresh_millihertz = 1;

    const auto result = f.provider->create_monitors(std::vector{bad});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DisplayError::InvalidArgument);
    EXPECT_TRUE(f.channel->requests.empty());
}

TEST(HelperVirtualDisplayProvider, ConfigureFailureStopsBeforeEnable) {
    Fixture f;
    f.channel->reply(helper::ResultCode::ConfigWriteFailed);

    const auto result = f.provider->create_monitors(std::vector{config()});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DisplayError::ConfigWriteFailed);
    EXPECT_EQ(f.channel->requests.size(), 1U);
}

TEST(HelperVirtualDisplayProvider, MapsHelperResultCodes) {
    EXPECT_EQ(osplay::engine::to_display_error(helper::ResultCode::VddNotInstalled),
              DisplayError::VddNotInstalled);
    EXPECT_EQ(osplay::engine::to_display_error(helper::ResultCode::DeviceControlFailed),
              DisplayError::DeviceControlFailed);
    EXPECT_EQ(osplay::engine::to_display_error(helper::ResultCode::UnsupportedVersion),
              DisplayError::ProtocolMismatch);
    EXPECT_EQ(osplay::engine::to_display_error(helper::ResultCode::ValueOutOfRange),
              DisplayError::InvalidArgument);
    EXPECT_EQ(osplay::engine::to_display_error(helper::ResultCode::NotConfigured),
              DisplayError::Rejected);
}

TEST(HelperVirtualDisplayProvider, TransportErrorsPassThrough) {
    Fixture f;
    f.channel->replies.emplace_back(
        win::Expected<helper::Bytes, DisplayError>::failure(DisplayError::ConnectionLost));

    const auto result = f.provider->remove_monitors();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DisplayError::ConnectionLost);
}

TEST(HelperVirtualDisplayProvider, GarbledResponsesAreProtocolMismatches) {
    Fixture f;
    f.channel->replies.emplace_back(helper::Bytes{0x01, 0x02, 0x03});

    const auto result = f.provider->status();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), DisplayError::ProtocolMismatch);
}

TEST(HelperVirtualDisplayProvider, StatusReturnsHelperState) {
    Fixture f;
    const helper::MonitorStatus state{.vdd_installed = true,
                                      .device_enabled = true,
                                      .configured = true,
                                      .width = 2560,
                                      .height = 1664,
                                      .refresh_millihertz = 60'000};
    f.channel->reply(helper::ResultCode::Ok, state);

    const auto result = f.provider->status();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), state);
}

} // namespace
