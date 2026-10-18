#include "host/host_orchestrator.hpp"

#include "engine_tunables.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace osplay::engine {

HostOrchestrator::HostOrchestrator(HostCorePort& core, VirtualDisplayProvider& displays,
                                   StreamPipelineFactory& pipelines, HostObserver& observer,
                                   std::unique_ptr<StreamDump> dump)
    : core_(&core), displays_(&displays), pipelines_(&pipelines), observer_(&observer),
      dump_(std::move(dump)), worker_([this] { run(); }) {}

HostOrchestrator::~HostOrchestrator() {
    try {
        post([this] { stop_streaming(); });
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        worker_.join();
    } catch (...) {
        // Only allocation, locking or joining can throw here, and an orchestrator that cannot
        // stop its worker is unrecoverable; this makes the implicit terminate explicit.
        std::terminate();
    }
}

void HostOrchestrator::on_viewer_monitors(std::span<const osplay_monitor> monitors) {
    if (monitors.empty()) {
        return;
    }
    // Copied: the core's strings are valid only during its callback.
    osplay_monitor first = monitors.front();
    first.name = nullptr;
    post([this, first] { start(first); });
}

void HostOrchestrator::on_keyframe_request(std::uint8_t stream_id) {
    post([this, stream_id] {
        if (stream_id == kHostStreamId && pipeline_) {
            pipeline_->request_keyframe();
        }
    });
}

void HostOrchestrator::on_session_closed() {
    post([this] { stop_streaming(); });
}

void HostOrchestrator::wait_idle() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return tasks_.empty() && !busy_; });
}

osplay_stream_config HostOrchestrator::stream_for(const osplay_monitor& monitor) {
    const double pixels_per_second = static_cast<double>(monitor.width_px) * monitor.height_px *
                                     monitor.refresh_millihertz / 1000.0;
    const auto kbps = static_cast<std::uint32_t>(pixels_per_second * kBitsPerPixel / 1000.0);
    return osplay_stream_config{
        .stream_id = kHostStreamId,
        .monitor_id = monitor.monitor_id,
        .width = monitor.width_px,
        .height = monitor.height_px,
        .refresh_millihertz = monitor.refresh_millihertz,
        .profile = OSPLAY_H264_HIGH,
        // Desktop Duplication delivers the pointer apart from the image (DEVPLAN).
        .cursor_mode = OSPLAY_CURSOR_SEPARATE,
        .bitrate_kbps = std::clamp(kbps, kMinBitrateKbps, kMaxBitrateKbps),
    };
}

void HostOrchestrator::post(std::function<void()> task) {
    {
        const std::scoped_lock lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    changed_.notify_all();
}

void HostOrchestrator::run() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty()) {
                return; // stopping, nothing left to do
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
            busy_ = true;
        }
        task();
        {
            const std::scoped_lock lock(mutex_);
            busy_ = false;
        }
        changed_.notify_all();
    }
}

void HostOrchestrator::start(const osplay_monitor& monitor) {
    if (pipeline_ || failed_) {
        return; // one stream per session on a Windows host
    }
    const osplay_stream_config stream = stream_for(monitor);
    const VirtualMonitorConfig config{.width = stream.width,
                                      .height = stream.height,
                                      .refresh_millihertz = stream.refresh_millihertz,
                                      .gpu_name = {}};
    if (const auto created = displays_->create_monitor(config); !created) {
        fail(HostFailure::Display, static_cast<std::uint32_t>(created.error()));
        return;
    }
    monitor_added_ = true;

    auto pipeline = pipelines_->start({.width = stream.width,
                                       .height = stream.height,
                                       .refresh_millihertz = stream.refresh_millihertz,
                                       .bitrate_kbps = stream.bitrate_kbps},
                                      *this);
    if (!pipeline) {
        fail(HostFailure::Pipeline, static_cast<std::uint32_t>(pipeline.error()));
        return;
    }
    pipeline_ = std::move(pipeline.value());
    if (const osplay_result configured = core_->configure_streams(std::span(&stream, 1));
        configured != OSPLAY_OK) {
        fail(HostFailure::Session, static_cast<std::uint32_t>(configured));
        return;
    }
    observer_->on_streaming(pipeline_->monitor_rect().value_or(MonitorRect{}));
}

void HostOrchestrator::stop_streaming() {
    pipeline_.reset();
    if (monitor_added_) {
        monitor_added_ = false;
        // Removal failures leave nothing to undo here; the helper removes the monitor anyway
        // when the engine's pipe closes.
        static_cast<void>(displays_->remove_monitor());
    }
}

void HostOrchestrator::fail(HostFailure failure, std::uint32_t detail) {
    if (failed_.exchange(true)) {
        return;
    }
    stop_streaming();
    observer_->on_host_failed(failure, detail);
    core_->close();
}

void HostOrchestrator::on_access_unit(std::span<const std::uint8_t> annexb, bool keyframe,
                                      std::uint64_t capture_time_us) {
    const osplay_access_unit unit{.stream_id = kHostStreamId,
                                  .keyframe = keyframe ? std::uint8_t{1} : std::uint8_t{0},
                                  .codec_config = keyframe ? std::uint8_t{1} : std::uint8_t{0},
                                  .frame_id = 0,
                                  .capture_time_us = capture_time_us,
                                  .data = annexb.data(),
                                  .length = annexb.size()};
    // Fails only while the session is closing; the frame is of no use then.
    static_cast<void>(core_->push_access_unit(unit));
    if (dump_) {
        dump_->write(annexb);
    }
}

void HostOrchestrator::on_cursor_shape(std::uint32_t shape_id, const CursorImage& image) {
    const osplay_cursor_shape shape{.stream_id = kHostStreamId,
                                    .shape_id = shape_id,
                                    .width = image.width,
                                    .height = image.height,
                                    .hotspot_x = image.hotspot_x,
                                    .hotspot_y = image.hotspot_y,
                                    .bgra = image.bgra.data(),
                                    .length = image.bgra.size()};
    static_cast<void>(core_->push_cursor_shape(shape));
}

void HostOrchestrator::on_cursor_position(bool visible, std::uint32_t shape_id, std::int32_t x,
                                          std::int32_t y, std::uint64_t capture_time_us) {
    const osplay_cursor_position position{.stream_id = kHostStreamId,
                                          .visible = visible ? std::uint8_t{1} : std::uint8_t{0},
                                          .shape_id = shape_id,
                                          .x = x,
                                          .y = y,
                                          .capture_time_us = capture_time_us};
    static_cast<void>(core_->push_cursor_position(position));
}

void HostOrchestrator::on_pipeline_failed(PipelineError error) {
    post([this, error] { fail(HostFailure::Pipeline, static_cast<std::uint32_t>(error)); });
}

} // namespace osplay::engine
