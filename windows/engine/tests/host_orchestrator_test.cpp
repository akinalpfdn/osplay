#include "host/host_orchestrator.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace {

using osplay::engine::CursorImage;
using osplay::engine::DisplayError;
using osplay::engine::HostCorePort;
using osplay::engine::HostFailure;
using osplay::engine::HostObserver;
using osplay::engine::HostOrchestrator;
using osplay::engine::MonitorRect;
using osplay::engine::PipelineError;
using osplay::engine::PipelineSink;
using osplay::engine::StreamDump;
using osplay::engine::StreamPipeline;
using osplay::engine::StreamPipelineFactory;
using osplay::engine::StreamSettings;
using osplay::engine::VirtualDisplayProvider;
using osplay::engine::VirtualMonitorConfig;
using osplay::engine::VirtualMonitorStatus;
namespace win = osplay::win;

class FakeCore final : public HostCorePort {
public:
    std::vector<osplay_stream_config> configured;
    std::vector<std::vector<std::uint8_t>> units;
    std::vector<std::uint32_t> shapes;
    std::vector<osplay_cursor_position> positions;
    bool closed = false;
    osplay_result configure_result = OSPLAY_OK;

    osplay_result configure_streams(std::span<const osplay_stream_config> streams) override {
        const std::scoped_lock lock(mutex);
        configured.assign(streams.begin(), streams.end());
        return configure_result;
    }
    osplay_result push_access_unit(const osplay_access_unit& unit) override {
        const std::scoped_lock lock(mutex);
        units.emplace_back(unit.data, unit.data + unit.length);
        return OSPLAY_OK;
    }
    osplay_result push_cursor_shape(const osplay_cursor_shape& shape) override {
        const std::scoped_lock lock(mutex);
        shapes.push_back(shape.shape_id);
        return OSPLAY_OK;
    }
    osplay_result push_cursor_position(const osplay_cursor_position& position) override {
        const std::scoped_lock lock(mutex);
        positions.push_back(position);
        return OSPLAY_OK;
    }
    void close() override {
        const std::scoped_lock lock(mutex);
        closed = true;
    }
    std::mutex mutex;
};

class FakeDisplays final : public VirtualDisplayProvider {
public:
    std::vector<VirtualMonitorConfig> created;
    int removed = 0;
    std::optional<DisplayError> create_error;

    win::Status<DisplayError> create_monitor(const VirtualMonitorConfig& config) override {
        if (create_error) {
            return win::Status<DisplayError>::failure(*create_error);
        }
        created.push_back(config);
        return win::Status<DisplayError>::ok();
    }
    win::Status<DisplayError> remove_monitor() override {
        ++removed;
        return win::Status<DisplayError>::ok();
    }
    win::Expected<VirtualMonitorStatus, DisplayError> status() override {
        return VirtualMonitorStatus{};
    }
};

class FakePipeline final : public StreamPipeline {
public:
    explicit FakePipeline(int& keyframe_requests) : keyframe_requests_(&keyframe_requests) {}
    void request_keyframe() override { ++*keyframe_requests_; }
    [[nodiscard]] std::optional<MonitorRect> monitor_rect() const override {
        return MonitorRect{.left = 1920, .top = 0, .right = 4480, .bottom = 1664};
    }

private:
    int* keyframe_requests_;
};

class FakePipelines final : public StreamPipelineFactory {
public:
    std::vector<StreamSettings> started;
    PipelineSink* sink = nullptr;
    int keyframe_requests = 0;
    int alive = 0;
    std::optional<PipelineError> start_error;

    win::Expected<std::unique_ptr<StreamPipeline>, PipelineError>
    start(const StreamSettings& settings, PipelineSink& output) override {
        if (start_error) {
            return win::Expected<std::unique_ptr<StreamPipeline>, PipelineError>::failure(
                *start_error);
        }
        started.push_back(settings);
        sink = &output;
        return std::unique_ptr<StreamPipeline>(std::make_unique<FakePipeline>(keyframe_requests));
    }
};

class RecordingObserver final : public HostObserver {
public:
    std::optional<MonitorRect> streaming;
    std::optional<HostFailure> failure;
    std::uint32_t detail = 0;

    void on_streaming(const MonitorRect& monitor) override { streaming = monitor; }
    void on_host_failed(HostFailure host_failure, std::uint32_t failure_detail) override {
        failure = host_failure;
        detail = failure_detail;
    }
};

osplay_monitor monitor(std::uint8_t id = 4) {
    return {.monitor_id = id,
            .width_px = 2560,
            .height_px = 1664,
            .refresh_millihertz = 60'000,
            .physical_width_mm = 302,
            .physical_height_mm = 196,
            .scale_percent = 200,
            .name = "Built-in Retina Display"};
}

struct Harness {
    FakeCore core;
    FakeDisplays displays;
    FakePipelines pipelines;
    RecordingObserver observer;
};

TEST(HostOrchestrator, TheFirstViewerMonitorBecomesTheVirtualMonitorAndTheStream) {
    Harness h;
    HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer);
    const std::vector<osplay_monitor> monitors{monitor(4), monitor(5)};
    orchestrator.on_viewer_monitors(monitors);
    orchestrator.wait_idle();

    ASSERT_EQ(h.displays.created.size(), 1U);
    EXPECT_EQ(h.displays.created[0].width, 2560U);
    EXPECT_EQ(h.displays.created[0].height, 1664U);
    EXPECT_EQ(h.displays.created[0].refresh_millihertz, 60'000U);
    ASSERT_EQ(h.pipelines.started.size(), 1U);
    EXPECT_EQ(h.pipelines.started[0].bitrate_kbps, 25'559U);
    ASSERT_EQ(h.core.configured.size(), 1U);
    EXPECT_EQ(h.core.configured[0].monitor_id, 4);
    EXPECT_EQ(h.core.configured[0].cursor_mode, OSPLAY_CURSOR_SEPARATE);
    ASSERT_TRUE(h.observer.streaming.has_value());
    EXPECT_EQ(h.observer.streaming->left, 1920);
}

TEST(HostOrchestrator, AHighRefreshMonitorGetsASixtyHertzStream) {
    Harness h;
    HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer);
    osplay_monitor fast = monitor(0);
    fast.refresh_millihertz = 120'000;
    orchestrator.on_viewer_monitors(std::vector<osplay_monitor>{fast});
    orchestrator.wait_idle();

    ASSERT_EQ(h.displays.created.size(), 1U);
    EXPECT_EQ(h.displays.created[0].refresh_millihertz, 60'000U);
    ASSERT_EQ(h.core.configured.size(), 1U);
    EXPECT_EQ(h.core.configured[0].refresh_millihertz, 60'000U);
}

TEST(HostOrchestrator, PipelineOutputReachesTheCoreAndTheDump) {
    Harness h;
    const auto directory = std::filesystem::temp_directory_path() / "osplay-orchestrator-test";
    std::filesystem::remove_all(directory);
    auto dump = std::make_unique<StreamDump>(directory, "stream.h264");
    const auto path = dump->path();
    {
        HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer, std::move(dump));
        const std::vector<osplay_monitor> monitors{monitor()};
        orchestrator.on_viewer_monitors(monitors);
        orchestrator.wait_idle();
        ASSERT_NE(h.pipelines.sink, nullptr);

        const std::vector<std::uint8_t> keyframe{0, 0, 0, 1, 0x67, 0, 0, 0, 1, 0x65};
        h.pipelines.sink->on_access_unit(keyframe, true, 42);
        h.pipelines.sink->on_cursor_shape(
            7, CursorImage{
                   .width = 1, .height = 1, .hotspot_x = 0, .hotspot_y = 0, .bgra = {1, 2, 3, 4}});
        h.pipelines.sink->on_cursor_position(true, 7, 100, 200, 43);

        orchestrator.on_keyframe_request(0);
        orchestrator.on_keyframe_request(3); // not a stream of this host
        orchestrator.wait_idle();
        EXPECT_EQ(h.pipelines.keyframe_requests, 1);
    }
    ASSERT_EQ(h.core.units.size(), 1U);
    EXPECT_EQ(h.core.shapes, (std::vector<std::uint32_t>{7}));
    ASSERT_EQ(h.core.positions.size(), 1U);
    EXPECT_EQ(h.core.positions[0].y, 200);
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> dumped((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
    EXPECT_EQ(dumped, h.core.units[0]);
    EXPECT_EQ(h.displays.removed, 1) << "the monitor is removed when the orchestrator ends";
}

TEST(HostOrchestrator, ASessionCloseRemovesTheMonitor) {
    Harness h;
    HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer);
    const std::vector<osplay_monitor> monitors{monitor()};
    orchestrator.on_viewer_monitors(monitors);
    orchestrator.on_session_closed();
    orchestrator.wait_idle();
    EXPECT_EQ(h.displays.removed, 1);
    EXPECT_FALSE(h.core.closed);
}

TEST(HostOrchestrator, AStoppedStreamRemovesTheMonitorButKeepsTheSession) {
    Harness h;
    HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer);
    const std::vector<osplay_monitor> monitors{monitor()};
    orchestrator.on_viewer_monitors(monitors);
    orchestrator.on_stream_stopped(1); // not this host's stream
    orchestrator.wait_idle();
    EXPECT_EQ(h.displays.removed, 0);
    orchestrator.on_stream_stopped(0);
    orchestrator.wait_idle();
    EXPECT_EQ(h.displays.removed, 1);
    EXPECT_FALSE(h.core.closed);
}

TEST(HostOrchestrator, ADisplayFailureClosesTheSessionWithTheReason) {
    Harness h;
    h.displays.create_error = DisplayError::HelperUnavailable;
    HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer);
    const std::vector<osplay_monitor> monitors{monitor()};
    orchestrator.on_viewer_monitors(monitors);
    orchestrator.wait_idle();
    EXPECT_EQ(h.observer.failure, HostFailure::Display);
    EXPECT_EQ(h.observer.detail, static_cast<std::uint32_t>(DisplayError::HelperUnavailable));
    EXPECT_TRUE(h.core.closed);
    EXPECT_TRUE(h.pipelines.started.empty());
}

TEST(HostOrchestrator, APipelineFailureRemovesTheMonitorAndClosesTheSession) {
    Harness h;
    HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer);
    const std::vector<osplay_monitor> monitors{monitor()};
    orchestrator.on_viewer_monitors(monitors);
    orchestrator.wait_idle();
    h.pipelines.sink->on_pipeline_failed(PipelineError::CaptureFailed);
    orchestrator.wait_idle();
    EXPECT_EQ(h.observer.failure, HostFailure::Pipeline);
    EXPECT_EQ(h.observer.detail, static_cast<std::uint32_t>(PipelineError::CaptureFailed));
    EXPECT_EQ(h.displays.removed, 1);
    EXPECT_TRUE(h.core.closed);
}

TEST(HostOrchestrator, AnEncoderThatCannotStartEndsTheSession) {
    Harness h;
    h.pipelines.start_error = PipelineError::EncoderUnavailable;
    HostOrchestrator orchestrator(h.core, h.displays, h.pipelines, h.observer);
    const std::vector<osplay_monitor> monitors{monitor()};
    orchestrator.on_viewer_monitors(monitors);
    orchestrator.wait_idle();
    EXPECT_EQ(h.observer.failure, HostFailure::Pipeline);
    EXPECT_EQ(h.displays.removed, 1);
    EXPECT_TRUE(h.core.closed);
}

TEST(HostOrchestrator, BitratesStayWithinLimits) {
    auto small = monitor();
    small.width_px = 640;
    small.height_px = 480;
    small.refresh_millihertz = 24'000;
    EXPECT_EQ(HostOrchestrator::stream_for(small).bitrate_kbps, 2'000U);
}

TEST(HostOrchestrator, TheQualityPresetScalesTheBitrateWithinItsRange) {
    EXPECT_EQ(HostOrchestrator::stream_for(monitor(), 0.06).bitrate_kbps, 15'335U);
    EXPECT_EQ(HostOrchestrator::stream_for(monitor(), 0.16).bitrate_kbps, 40'894U);
    // Out-of-range presets are clamped to 0.3 bits per pixel.
    EXPECT_EQ(HostOrchestrator::stream_for(monitor(), 1.0).bitrate_kbps, 76'677U);
}

} // namespace
