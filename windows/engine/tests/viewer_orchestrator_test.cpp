#include "viewer/viewer_orchestrator.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using osplay::engine::MonitorRect;
using osplay::engine::PipelineError;
using osplay::engine::ViewerCorePort;
using osplay::engine::ViewerMonitor;
using osplay::engine::ViewerObserver;
using osplay::engine::ViewerOrchestrator;
using osplay::engine::ViewerStream;
using osplay::engine::ViewerStreamFactory;
using osplay::engine::ViewerStreamSink;
using osplay::engine::ViewerStreamStats;
namespace win = osplay::win;

class FakeCore final : public ViewerCorePort {
public:
    std::vector<std::uint8_t> keyframes;
    std::vector<std::uint8_t> paused;
    std::vector<std::uint8_t> resumed;
    std::vector<std::uint8_t> stopped;

    osplay_result request_keyframe(std::uint8_t id) override { return record(keyframes, id); }
    osplay_result pause_stream(std::uint8_t id) override { return record(paused, id); }
    osplay_result resume_stream(std::uint8_t id) override { return record(resumed, id); }
    osplay_result stop_stream(std::uint8_t id) override { return record(stopped, id); }

private:
    osplay_result record(std::vector<std::uint8_t>& list, std::uint8_t id) {
        const std::scoped_lock lock(mutex_);
        list.push_back(id);
        return OSPLAY_OK;
    }
    std::mutex mutex_;
};

/// Shared with the test after the orchestrator takes ownership of the stream.
struct StreamRecord {
    std::uint8_t monitor_id = 0;
    std::atomic<int> units{0};
    std::atomic<bool> visible{true};
    std::atomic<bool> alive{true};
    ViewerStreamSink* sink = nullptr;

    std::string overlay() const {
        const std::scoped_lock lock(mutex);
        return overlay_text;
    }
    mutable std::mutex mutex;
    std::string overlay_text;
};

class FakeStream final : public ViewerStream {
public:
    explicit FakeStream(std::shared_ptr<StreamRecord> record) : record_(std::move(record)) {}
    ~FakeStream() override { record_->alive = false; }
    FakeStream(const FakeStream&) = delete;
    FakeStream& operator=(const FakeStream&) = delete;
    FakeStream(FakeStream&&) = delete;
    FakeStream& operator=(FakeStream&&) = delete;

    void submit(std::span<const std::uint8_t> /*annexb*/, bool /*keyframe*/,
                std::uint64_t /*capture*/) override {
        ++record_->units;
    }
    void set_visible(bool visible) override { record_->visible = visible; }
    void set_overlay(const std::string& text) override {
        const std::scoped_lock lock(record_->mutex);
        record_->overlay_text = text;
    }
    [[nodiscard]] ViewerStreamStats stats() const override {
        return {.frames_presented = static_cast<std::uint64_t>(record_->units.load()),
                .decode_errors = 0};
    }

private:
    std::shared_ptr<StreamRecord> record_;
};

class FakeFactory final : public ViewerStreamFactory {
public:
    std::vector<std::shared_ptr<StreamRecord>> records;
    std::optional<PipelineError> error;

    win::Expected<std::unique_ptr<ViewerStream>, PipelineError>
    start(const osplay_stream_config& /*stream*/, const ViewerMonitor& monitor, bool visible,
          ViewerStreamSink& sink) override {
        if (error) {
            return win::Expected<std::unique_ptr<ViewerStream>, PipelineError>::failure(*error);
        }
        auto record = std::make_shared<StreamRecord>();
        record->monitor_id = monitor.id;
        record->visible = visible;
        record->sink = &sink;
        records.push_back(record);
        return std::unique_ptr<ViewerStream>(std::make_unique<FakeStream>(record));
    }
};

class RecordingObserver final : public ViewerObserver {
public:
    std::vector<bool> visibility;
    std::vector<std::pair<std::uint8_t, PipelineError>> failures;
    void on_visibility(bool visible) override { visibility.push_back(visible); }
    void on_stream_failed(std::uint8_t stream_id, PipelineError error) override {
        failures.emplace_back(stream_id, error);
    }
};

ViewerMonitor monitor(std::uint8_t id, std::int32_t left) {
    return {.id = id,
            .rect = MonitorRect{.left = left, .top = 0, .right = left + 2560, .bottom = 1440},
            .description = {}};
}

osplay_stream_config stream(std::uint8_t stream_id, std::uint8_t monitor_id) {
    return {.stream_id = stream_id,
            .monitor_id = monitor_id,
            .width = 2560,
            .height = 1440,
            .refresh_millihertz = 60'000,
            .profile = OSPLAY_H264_HIGH,
            .cursor_mode = OSPLAY_CURSOR_IN_VIDEO,
            .bitrate_kbps = 22'000};
}

osplay_access_unit unit(std::uint8_t stream_id) {
    static const std::uint8_t kData[]{0, 0, 0, 1, 0x65}; // NOLINT(*-avoid-c-arrays): test bytes
    return {.stream_id = stream_id,
            .keyframe = 1,
            .codec_config = 1,
            .frame_id = 0,
            .capture_time_us = 1,
            .data = static_cast<const std::uint8_t*>(kData),
            .length = sizeof(kData)};
}

struct Harness {
    FakeCore core;
    FakeFactory factory;
    RecordingObserver observer;
    ViewerOrchestrator orchestrator{core, factory, observer, {monitor(0, 0), monitor(1, 2560)}};
};

TEST(ViewerOrchestrator, EachStreamIsPresentedOnTheMonitorTheHostChose) {
    Harness h;
    h.orchestrator.on_stream_config(stream(0, 1));
    h.orchestrator.on_stream_config(stream(1, 0));
    h.orchestrator.wait_idle();
    ASSERT_EQ(h.factory.records.size(), 2U);
    EXPECT_EQ(h.factory.records[0]->monitor_id, 1);
    EXPECT_EQ(h.factory.records[1]->monitor_id, 0);

    h.orchestrator.on_access_unit(unit(0));
    h.orchestrator.on_access_unit(unit(0));
    h.orchestrator.on_access_unit(unit(7)); // unknown stream: ignored
    EXPECT_EQ(h.factory.records[0]->units, 2);
    EXPECT_EQ(h.orchestrator.stats().at(0).frames_presented, 2U);
}

TEST(ViewerOrchestrator, TheOverlayReachesRunningStreamsAndStreamsThatStartLater) {
    Harness h;
    h.orchestrator.set_overlay(1, "early");
    h.orchestrator.on_stream_config(stream(0, 0));
    h.orchestrator.on_stream_config(stream(1, 1));
    h.orchestrator.wait_idle();
    ASSERT_EQ(h.factory.records.size(), 2U);
    EXPECT_EQ(h.factory.records[0]->overlay(), "");
    EXPECT_EQ(h.factory.records[1]->overlay(), "early");

    h.orchestrator.set_overlay(0, "60 fps");
    h.orchestrator.set_overlay(1, "");
    EXPECT_EQ(h.factory.records[0]->overlay(), "60 fps");
    EXPECT_EQ(h.factory.records[1]->overlay(), "");
}

TEST(ViewerOrchestrator, AStreamForAnUnknownMonitorIsRefused) {
    Harness h;
    h.orchestrator.on_stream_config(stream(0, 9));
    h.orchestrator.wait_idle();
    EXPECT_TRUE(h.factory.records.empty());
    ASSERT_EQ(h.observer.failures.size(), 1U);
    EXPECT_EQ(h.observer.failures[0].second, PipelineError::MonitorNotFound);
    EXPECT_EQ(h.core.stopped, (std::vector<std::uint8_t>{0}));
}

TEST(ViewerOrchestrator, HidingPausesEveryStreamAndShowingResumes) {
    Harness h;
    h.orchestrator.on_stream_config(stream(0, 0));
    h.orchestrator.on_stream_config(stream(1, 1));
    h.orchestrator.toggle_visibility();
    h.orchestrator.wait_idle();
    EXPECT_FALSE(h.factory.records[0]->visible);
    EXPECT_EQ(h.core.paused, (std::vector<std::uint8_t>{0, 1}));
    EXPECT_EQ(h.observer.visibility, (std::vector<bool>{false}));

    h.orchestrator.toggle_visibility();
    h.orchestrator.wait_idle();
    EXPECT_TRUE(h.factory.records[1]->visible);
    EXPECT_EQ(h.core.resumed, (std::vector<std::uint8_t>{0, 1}));
}

TEST(ViewerOrchestrator, AStreamConfiguredWhileHiddenStartsPaused) {
    Harness h;
    h.orchestrator.set_visible(false);
    h.orchestrator.on_stream_config(stream(0, 0));
    h.orchestrator.wait_idle();
    EXPECT_FALSE(h.factory.records[0]->visible);
    EXPECT_EQ(h.core.paused, (std::vector<std::uint8_t>{0}));
}

TEST(ViewerOrchestrator, AnUnpluggedMonitorStopsOnlyItsStream) {
    Harness h;
    h.orchestrator.on_stream_config(stream(0, 0));
    h.orchestrator.on_stream_config(stream(1, 1));
    h.orchestrator.on_monitors_changed({0});
    h.orchestrator.wait_idle();
    EXPECT_TRUE(h.factory.records[0]->alive);
    EXPECT_FALSE(h.factory.records[1]->alive);
    EXPECT_EQ(h.core.stopped, (std::vector<std::uint8_t>{1}));
}

TEST(ViewerOrchestrator, DecoderTroubleAsksForAKeyframe) {
    Harness h;
    h.orchestrator.on_stream_config(stream(0, 0));
    h.orchestrator.wait_idle();
    h.factory.records[0]->sink->on_keyframe_needed(0);
    h.orchestrator.wait_idle();
    EXPECT_EQ(h.core.keyframes, (std::vector<std::uint8_t>{0}));
}

TEST(ViewerOrchestrator, AStreamTheHostStopsClosesWithoutAReply) {
    Harness h;
    h.orchestrator.on_stream_config(stream(0, 0));
    h.orchestrator.on_stream_stopped(0);
    h.orchestrator.wait_idle();
    EXPECT_FALSE(h.factory.records[0]->alive);
    EXPECT_TRUE(h.core.stopped.empty());
}

TEST(ViewerOrchestrator, ASessionCloseClosesEveryStream) {
    Harness h;
    h.orchestrator.on_stream_config(stream(0, 0));
    h.orchestrator.on_stream_config(stream(1, 1));
    h.orchestrator.on_session_closed();
    h.orchestrator.wait_idle();
    EXPECT_FALSE(h.factory.records[0]->alive);
    EXPECT_FALSE(h.factory.records[1]->alive);
}

} // namespace
