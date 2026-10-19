#include "viewer/viewer_orchestrator.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>

namespace osplay::engine {

ViewerOrchestrator::ViewerOrchestrator(ViewerCorePort& core, ViewerStreamFactory& streams,
                                       ViewerObserver& observer,
                                       std::vector<ViewerMonitor> monitors)
    : core_(&core), factories_(&streams), observer_(&observer), monitors_(std::move(monitors)),
      worker_([this] { run(); }) {}

ViewerOrchestrator::~ViewerOrchestrator() {
    try {
        on_session_closed();
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

void ViewerOrchestrator::on_stream_config(const osplay_stream_config& stream) {
    post([this, stream] { start(stream); });
}

void ViewerOrchestrator::on_access_unit(const osplay_access_unit& unit) {
    const std::scoped_lock lock(streams_mutex_);
    if (const auto it = streams_.find(unit.stream_id); it != streams_.end()) {
        it->second->submit(std::span(unit.data, unit.length), unit.keyframe == 1,
                           unit.capture_time_us);
    }
}

void ViewerOrchestrator::on_stream_stopped(std::uint8_t stream_id) {
    post([this, stream_id] { stop(stream_id, false); });
}

void ViewerOrchestrator::on_session_closed() {
    post([this] {
        std::map<std::uint8_t, std::unique_ptr<ViewerStream>> closing;
        {
            const std::scoped_lock lock(streams_mutex_);
            closing.swap(streams_);
            monitor_of_.clear();
        }
        // Destroyed outside the lock: closing a stream joins its thread.
    });
}

void ViewerOrchestrator::set_visible(bool visible) {
    post([this, visible] { apply_visibility(visible); });
}

void ViewerOrchestrator::toggle_visibility() {
    post([this] { apply_visibility(!visible_); });
}

void ViewerOrchestrator::on_monitors_changed(const std::set<std::uint8_t>& attached) {
    // Shared, so the task stays cheap to copy and cannot throw while being moved around.
    auto still_attached = std::make_shared<const std::set<std::uint8_t>>(attached);
    post([this, still_attached] {
        std::vector<std::uint8_t> orphaned;
        {
            const std::scoped_lock lock(streams_mutex_);
            for (const auto& [stream_id, monitor_id] : monitor_of_) {
                if (streams_.contains(stream_id) && !still_attached->contains(monitor_id)) {
                    orphaned.push_back(stream_id);
                }
            }
        }
        for (const std::uint8_t stream_id : orphaned) {
            stop(stream_id, true); // the host stops only this stream
        }
    });
}

std::map<std::uint8_t, ViewerStreamStats> ViewerOrchestrator::stats() const {
    const std::scoped_lock lock(streams_mutex_);
    std::map<std::uint8_t, ViewerStreamStats> result;
    for (const auto& [stream_id, stream] : streams_) {
        result.emplace(stream_id, stream->stats());
    }
    return result;
}

void ViewerOrchestrator::wait_idle() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return tasks_.empty() && !busy_; });
}

void ViewerOrchestrator::post(std::function<void()> task) {
    {
        const std::scoped_lock lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    changed_.notify_all();
}

void ViewerOrchestrator::run() {
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

void ViewerOrchestrator::start(const osplay_stream_config& stream) {
    const auto monitor = std::ranges::find_if(
        monitors_, [&](const ViewerMonitor& m) { return m.id == stream.monitor_id; });
    if (monitor == monitors_.end()) {
        observer_->on_stream_failed(stream.stream_id, PipelineError::MonitorNotFound);
        static_cast<void>(core_->stop_stream(stream.stream_id));
        return;
    }
    std::unique_ptr<ViewerStream> replaced;
    {
        const std::scoped_lock lock(streams_mutex_);
        if (auto it = streams_.find(stream.stream_id); it != streams_.end()) {
            replaced = std::move(it->second);
            streams_.erase(it);
        }
    }
    replaced.reset(); // a new configuration for the same stream replaces the old one

    auto started = factories_->start(stream, *monitor, visible_, *this);
    if (!started) {
        observer_->on_stream_failed(stream.stream_id, started.error());
        static_cast<void>(core_->stop_stream(stream.stream_id));
        return;
    }
    {
        const std::scoped_lock lock(streams_mutex_);
        streams_[stream.stream_id] = std::move(started.value());
        monitor_of_[stream.stream_id] = stream.monitor_id;
    }
    if (!visible_) {
        static_cast<void>(core_->pause_stream(stream.stream_id));
    }
}

void ViewerOrchestrator::apply_visibility(bool visible) {
    if (visible == visible_) {
        return;
    }
    visible_ = visible;
    std::vector<std::uint8_t> ids;
    {
        const std::scoped_lock lock(streams_mutex_);
        for (const auto& [stream_id, stream] : streams_) {
            stream->set_visible(visible);
            ids.push_back(stream_id);
        }
    }
    for (const std::uint8_t stream_id : ids) {
        // Fails only while the session is closing.
        static_cast<void>(visible ? core_->resume_stream(stream_id)
                                  : core_->pause_stream(stream_id));
    }
    observer_->on_visibility(visible);
}

void ViewerOrchestrator::stop(std::uint8_t stream_id, bool tell_host) {
    std::unique_ptr<ViewerStream> stopped;
    {
        const std::scoped_lock lock(streams_mutex_);
        if (auto it = streams_.find(stream_id); it != streams_.end()) {
            stopped = std::move(it->second);
            streams_.erase(it);
        }
        monitor_of_.erase(stream_id);
    }
    if (stopped && tell_host) {
        static_cast<void>(core_->stop_stream(stream_id));
    }
}

void ViewerOrchestrator::on_keyframe_needed(std::uint8_t stream_id) {
    post([this, stream_id] { static_cast<void>(core_->request_keyframe(stream_id)); });
}

} // namespace osplay::engine
