#pragma once

// Viewer session orchestration in the engine (DEVPLAN Phase 9): one decoder and presenter per
// stream on the monitor the host chose, keyframe requests after decoder errors, hide/show (hidden
// streams are paused so the host stops sending), and a monitor that disappears stops only its own
// stream. Mirrors ViewerSession on macOS; portable and unit tested.

#include "viewer/viewer_stream.hpp"

#include <osplay/osplay.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <thread>
#include <vector>

namespace osplay::engine {

/// The part of a core viewer session the orchestrator drives.
class ViewerCorePort {
public:
    virtual ~ViewerCorePort() = default;
    virtual osplay_result request_keyframe(std::uint8_t stream_id) = 0;
    virtual osplay_result pause_stream(std::uint8_t stream_id) = 0;
    virtual osplay_result resume_stream(std::uint8_t stream_id) = 0;
    virtual osplay_result stop_stream(std::uint8_t stream_id) = 0;
};

class ViewerObserver {
public:
    virtual ~ViewerObserver() = default;
    /// Called on the orchestrator's worker thread.
    virtual void on_visibility(bool visible) = 0;
    virtual void on_stream_failed(std::uint8_t stream_id, PipelineError error) = 0;
};

class ViewerOrchestrator final : private ViewerStreamSink {
public:
    ViewerOrchestrator(ViewerCorePort& core, ViewerStreamFactory& streams, ViewerObserver& observer,
                       std::vector<ViewerMonitor> monitors);
    ~ViewerOrchestrator() override;
    ViewerOrchestrator(const ViewerOrchestrator&) = delete;
    ViewerOrchestrator& operator=(const ViewerOrchestrator&) = delete;
    ViewerOrchestrator(ViewerOrchestrator&&) = delete;
    ViewerOrchestrator& operator=(ViewerOrchestrator&&) = delete;

    // Core events, from the core's I/O thread.
    void on_stream_config(const osplay_stream_config& stream);
    /// Goes straight to the stream (which copies it); never waits for the worker.
    void on_access_unit(const osplay_access_unit& unit);
    void on_stream_stopped(std::uint8_t stream_id);
    void on_session_closed();

    // Platform events, from any thread.
    void set_visible(bool visible);
    void toggle_visibility();
    /// The monitors currently attached; streams on any other monitor are stopped.
    void on_monitors_changed(const std::set<std::uint8_t>& attached);

    [[nodiscard]] std::map<std::uint8_t, ViewerStreamStats> stats() const;
    /// Blocks until queued work has run (tests).
    void wait_idle();

private:
    void post(std::function<void()> task);
    void run();
    void start(const osplay_stream_config& stream);
    void apply_visibility(bool visible);
    void stop(std::uint8_t stream_id, bool tell_host);
    void on_keyframe_needed(std::uint8_t stream_id) override;

    ViewerCorePort* core_;           // never null; outlives the orchestrator
    ViewerStreamFactory* factories_; // never null
    ViewerObserver* observer_;       // never null
    std::vector<ViewerMonitor> monitors_;
    bool visible_ = true; // worker thread only

    mutable std::mutex streams_mutex_; // guards streams_: access units arrive on the I/O thread
    std::map<std::uint8_t, std::unique_ptr<ViewerStream>> streams_;
    std::map<std::uint8_t, std::uint8_t> monitor_of_; // stream → monitor

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> tasks_;
    bool busy_ = false;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace osplay::engine
