#include <osplay/win/helper_request_handler.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace osplay::win::helper; // NOLINT(google-build-using-namespace)

/// The driver's devices; one disabled device, as the driver's installer leaves it.
class FakeDevices final : public VirtualMonitorDevices {
public:
    std::vector<bool> enabled{false};
    bool fail_changes = false;
    bool fail_add = false;
    bool added_devices_appear = true;
    int added = 0;
    std::vector<std::pair<std::size_t, bool>> changes;

    bool is_installed() override { return !enabled.empty(); }
    std::size_t count() override { return enabled.size(); }
    bool add() override {
        if (fail_add) {
            return false;
        }
        if (added_devices_appear) {
            enabled.push_back(true);
        }
        ++added;
        return true;
    }
    bool is_enabled(std::size_t index) override { return enabled.at(index); }
    bool set_enabled(std::size_t index, bool value) override {
        if (fail_changes) {
            return false;
        }
        changes.emplace_back(index, value);
        enabled.at(index) = value;
        return true;
    }
};

class FakeSettings final : public SettingsStore {
public:
    bool fail = false;
    std::vector<std::string> written;

    bool write(const std::string& contents) override {
        if (fail) {
            return false;
        }
        written.push_back(contents);
        return true;
    }
};

class FakeLog final : public HelperLog {
public:
    std::vector<std::string> infos;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    void info(std::string_view message) override { infos.emplace_back(message); }
    void warning(std::string_view message) override { warnings.emplace_back(message); }
    void error(std::string_view message) override { errors.emplace_back(message); }
};

class RequestHandlerTest : public ::testing::Test {
protected:
    FakeDevices devices;
    FakeSettings settings;
    FakeLog log;
    RequestHandler handler{devices, settings, log};

    static Request configure_request(std::size_t monitors = 1) {
        return Request{
            .command = Command::Configure,
            .monitors = std::vector<MonitorConfig>(
                monitors,
                {.width = 2560, .height = 1664, .refresh_millihertz = 60'000, .gpu_name = ""}),
        };
    }
    static Request simple(Command command) { return Request{.command = command, .monitors = {}}; }
};

TEST_F(RequestHandlerTest, ConfigureWritesSettingsAndEnableAddsMonitor) {
    EXPECT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(settings.written.size(), 1U);
    EXPECT_NE(settings.written[0].find("<width>2560</width>"), std::string::npos);

    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    EXPECT_EQ(devices.enabled, (std::vector<bool>{true}));
}

TEST_F(RequestHandlerTest, EnableWithoutConfigurationIsRefused) {
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::NotConfigured);
    EXPECT_EQ(devices.enabled, (std::vector<bool>{false}));
}

TEST_F(RequestHandlerTest, MissingDriverIsReported) {
    devices.enabled.clear();
    EXPECT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::VddNotInstalled);
    EXPECT_EQ(handler.handle(simple(Command::Disable)).result, ResultCode::VddNotInstalled);
    EXPECT_FALSE(handler.handle(simple(Command::Status)).status.vdd_installed);
}

TEST_F(RequestHandlerTest, ReconfiguringAnActiveMonitorRestartsTheDevice) {
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    devices.changes.clear();

    EXPECT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    EXPECT_EQ(devices.changes, (std::vector<std::pair<std::size_t, bool>>{{0, false}, {0, true}}));
}

TEST_F(RequestHandlerTest, DisableRemovesMonitor) {
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);

    EXPECT_EQ(handler.handle(simple(Command::Disable)).result, ResultCode::Ok);
    EXPECT_EQ(devices.enabled, (std::vector<bool>{false}));
}

TEST_F(RequestHandlerTest, DisableWithoutMonitorChangesNothingAndLogsNothing) {
    EXPECT_EQ(handler.handle(simple(Command::Disable)).result, ResultCode::Ok);
    EXPECT_TRUE(devices.changes.empty());
    EXPECT_TRUE(log.infos.empty());
}

TEST_F(RequestHandlerTest, ResetRemovesMonitorAndForgetsConfiguration) {
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);

    handler.reset();

    EXPECT_EQ(devices.enabled, (std::vector<bool>{false}));
    EXPECT_FALSE(handler.handle(simple(Command::Status)).status.configured);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::NotConfigured);
}

TEST_F(RequestHandlerTest, ResetDisablesMonitorsLeftEnabledBeforeTheServiceStarted) {
    devices.enabled = {true, false, true};
    handler.reset();
    EXPECT_EQ(devices.enabled, (std::vector<bool>{false, false, false}));
}

TEST_F(RequestHandlerTest, EnableGivesEveryMonitorItsOwnDevice) {
    ASSERT_EQ(handler.handle(configure_request(3)).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    EXPECT_EQ(devices.added, 2);
    EXPECT_EQ(devices.enabled, (std::vector<bool>{true, true, true}));
    EXPECT_TRUE(handler.handle(simple(Command::Status)).status.device_enabled);
}

TEST_F(RequestHandlerTest, EnableReusesDevicesAndLeavesTheSpareOnesOff) {
    devices.enabled = {false, false, false};
    ASSERT_EQ(handler.handle(configure_request(2)).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    EXPECT_EQ(devices.added, 0);
    EXPECT_EQ(devices.enabled, (std::vector<bool>{true, true, false}));
}

TEST_F(RequestHandlerTest, FewerMonitorsTurnTheDevicesNoLongerNeededOff) {
    ASSERT_EQ(handler.handle(configure_request(2)).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    devices.changes.clear();

    EXPECT_EQ(handler.handle(configure_request(1)).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    EXPECT_EQ(devices.enabled, (std::vector<bool>{true, false}));
    EXPECT_EQ(devices.changes,
              (std::vector<std::pair<std::size_t, bool>>{{0, false}, {0, true}, {1, false}}))
        << "the device in use restarts to read the new settings";
}

TEST_F(RequestHandlerTest, DisableRemovesEveryMonitor) {
    ASSERT_EQ(handler.handle(configure_request(2)).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Disable)).result, ResultCode::Ok);
    EXPECT_EQ(devices.enabled, (std::vector<bool>{false, false}));
    EXPECT_FALSE(handler.handle(simple(Command::Status)).status.device_enabled);
}

TEST_F(RequestHandlerTest, AddedDevicesThatDoNotAppearAreNotAddedAgain) {
    devices.added_devices_appear = false;
    ASSERT_EQ(handler.handle(configure_request(3)).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::DeviceControlFailed);
    EXPECT_EQ(devices.added, 2);
}

TEST_F(RequestHandlerTest, ADeviceThatCannotBeAddedIsReported) {
    devices.fail_add = true;
    ASSERT_EQ(handler.handle(configure_request(2)).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::DeviceControlFailed);
    EXPECT_EQ(log.errors.size(), 1U);
}

TEST_F(RequestHandlerTest, StatusReportsConfiguration) {
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    const Response response = handler.handle(simple(Command::Status));

    EXPECT_TRUE(response.status.vdd_installed);
    EXPECT_FALSE(response.status.device_enabled);
    EXPECT_TRUE(response.status.configured);
    EXPECT_EQ(response.status.width, 2560U);
    EXPECT_EQ(response.status.height, 1664U);
    EXPECT_EQ(response.status.refresh_millihertz, 60'000U);
}

TEST_F(RequestHandlerTest, FailuresAreReportedAndLogged) {
    settings.fail = true;
    EXPECT_EQ(handler.handle(configure_request()).result, ResultCode::ConfigWriteFailed);

    settings.fail = false;
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    devices.fail_changes = true;
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::DeviceControlFailed);
    EXPECT_EQ(log.errors.size(), 2U);
}

TEST_F(RequestHandlerTest, MalformedMessagesAreRejectedAndLogged) {
    Bytes message = encode(configure_request());
    // After the header: count, reserved, GPU name length 0, then the first monitor's width.
    message[16] = 0x01; // width becomes 1
    message[17] = 0x00;
    message[18] = 0x00;
    message[19] = 0x00;

    const auto response = decode_response(handler.handle(message));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().result, ResultCode::ValueOutOfRange);
    EXPECT_TRUE(settings.written.empty());
    ASSERT_EQ(log.warnings.size(), 1U);
    EXPECT_NE(log.warnings[0].find("value out of range"), std::string::npos);
}

TEST_F(RequestHandlerTest, OutOfRangeInProcessRequestsAreRejected) {
    Request request = configure_request();
    request.monitors.front().width = 100;
    EXPECT_EQ(handler.handle(request).result, ResultCode::ValueOutOfRange);
    EXPECT_TRUE(settings.written.empty());
}

} // namespace
