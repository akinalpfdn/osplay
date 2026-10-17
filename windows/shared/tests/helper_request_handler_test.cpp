#include <osplay/win/helper_request_handler.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using namespace osplay::win::helper; // NOLINT(google-build-using-namespace)

class FakeDevice final : public VirtualMonitorDevice {
public:
    bool installed = true;
    bool enabled = false;
    bool fail_changes = false;
    std::vector<bool> changes;

    bool is_installed() override { return installed; }
    bool is_enabled() override { return enabled; }
    bool set_enabled(bool value) override {
        if (fail_changes) {
            return false;
        }
        changes.push_back(value);
        enabled = value;
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
    FakeDevice device;
    FakeSettings settings;
    FakeLog log;
    RequestHandler handler{device, settings, log};

    static Request configure_request() {
        return Request{
            .command = Command::Configure,
            .config = {.width = 2560, .height = 1664, .refresh_millihertz = 60'000, .gpu_name = ""},
        };
    }
    static Request simple(Command command) { return Request{.command = command, .config = {}}; }
};

TEST_F(RequestHandlerTest, ConfigureWritesSettingsAndEnableAddsMonitor) {
    EXPECT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(settings.written.size(), 1U);
    EXPECT_NE(settings.written[0].find("<width>2560</width>"), std::string::npos);

    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    EXPECT_TRUE(device.enabled);
}

TEST_F(RequestHandlerTest, EnableWithoutConfigurationIsRefused) {
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::NotConfigured);
    EXPECT_FALSE(device.enabled);
}

TEST_F(RequestHandlerTest, MissingDriverIsReported) {
    device.installed = false;
    EXPECT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::VddNotInstalled);
    EXPECT_EQ(handler.handle(simple(Command::Disable)).result, ResultCode::VddNotInstalled);
    EXPECT_FALSE(handler.handle(simple(Command::Status)).status.vdd_installed);
}

TEST_F(RequestHandlerTest, ReconfiguringAnActiveMonitorRestartsTheDevice) {
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);
    device.changes.clear();

    EXPECT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    EXPECT_EQ(device.changes, (std::vector<bool>{false, true}));
}

TEST_F(RequestHandlerTest, DisableRemovesMonitor) {
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);

    EXPECT_EQ(handler.handle(simple(Command::Disable)).result, ResultCode::Ok);
    EXPECT_FALSE(device.enabled);
}

TEST_F(RequestHandlerTest, DisableWithoutMonitorChangesNothingAndLogsNothing) {
    EXPECT_EQ(handler.handle(simple(Command::Disable)).result, ResultCode::Ok);
    EXPECT_TRUE(device.changes.empty());
    EXPECT_TRUE(log.infos.empty());
}

TEST_F(RequestHandlerTest, ResetRemovesMonitorAndForgetsConfiguration) {
    ASSERT_EQ(handler.handle(configure_request()).result, ResultCode::Ok);
    ASSERT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::Ok);

    handler.reset();

    EXPECT_FALSE(device.enabled);
    EXPECT_FALSE(handler.handle(simple(Command::Status)).status.configured);
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::NotConfigured);
}

TEST_F(RequestHandlerTest, ResetDisablesAMonitorLeftEnabledBeforeTheServiceStarted) {
    device.enabled = true;
    handler.reset();
    EXPECT_FALSE(device.enabled);
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
    device.fail_changes = true;
    EXPECT_EQ(handler.handle(simple(Command::Enable)).result, ResultCode::DeviceControlFailed);
    EXPECT_EQ(log.errors.size(), 2U);
}

TEST_F(RequestHandlerTest, MalformedMessagesAreRejectedAndLogged) {
    Bytes message = encode(configure_request());
    message[12] = 0x01; // width becomes 1
    message[13] = 0x00;
    message[14] = 0x00;
    message[15] = 0x00;

    const auto response = decode_response(handler.handle(message));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response.value().result, ResultCode::ValueOutOfRange);
    EXPECT_TRUE(settings.written.empty());
    ASSERT_EQ(log.warnings.size(), 1U);
    EXPECT_NE(log.warnings[0].find("value out of range"), std::string::npos);
}

TEST_F(RequestHandlerTest, OutOfRangeInProcessRequestsAreRejected) {
    Request request = configure_request();
    request.config.width = 100;
    EXPECT_EQ(handler.handle(request).result, ResultCode::ValueOutOfRange);
    EXPECT_TRUE(settings.written.empty());
}

} // namespace
