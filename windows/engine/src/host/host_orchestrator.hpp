#pragma once

// Host session orchestration in the engine (DEVPLAN Phase 8): when the viewer offers its monitors,
// add the virtual monitor in the first one's mode, start capture and encoding, configure the
// stream, and feed access units and the cursor to the core. Mirrors HostSession on macOS.

#include "display/virtual_display_provider.hpp"
#include "engine_tunables.hpp"
#include "host/stream_dump.hpp"
#include "host/stream_pipeline.hpp"

#include <osplay/osplay.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace osplay::engine {

/// The part of a core host session the orchestrator drives; the C API session in the engine.
class HostCorePort {
public:
    virtual ~HostCorePort() = default;
    virtual osplay_result configure_streams(std::span<const osplay_stream_config> streams) = 0;
    virtual osplay_result push_access_unit(const osplay_access_unit& unit) = 0;
    virtual osplay_result push_cursor_shape(const osplay_cursor_shape& shape) = 0;
    virtual osplay_result push_cursor_position(const osplay_cursor_position& position) = 0;
    /// Ends the session with Bye; the core reports the close later.
    virtual void close() = 0;
};

/// Why the orchestrator ended a session.
enum class HostFailure : std::uint8_t {
    Display,  ///< the virtual monitor could not be added
    Pipeline, ///< capture or encoding failed
    Session,  ///< the core refused the stream configuration
};

class HostObserver {
public:
    virtual ~HostObserver() = default;
    /// Called on the orchestrator's worker thread.
    virtual void on_streaming(const MonitorRect& monitor) = 0;
    virtual void on_host_failed(HostFailure failure, std::uint32_t detail) = 0;
};

class HostOrchestrator final : private PipelineSink {
public:
    HostOrchestrator(HostCorePort& core, VirtualDisplayProvider& displays,
                     StreamPipelineFactory& pipelines, HostObserver& observer,
                     std::unique_ptr<StreamDump> dump = nullptr,
                     double bits_per_pixel = kBitsPerPixel);
    ~HostOrchestrator() override;
    HostOrchestrator(const HostOrchestrator&) = delete;
    HostOrchestrator& operator=(const HostOrchestrator&) = delete;
    HostOrchestrator(HostOrchestrator&&) = delete;
    HostOrchestrator& operator=(HostOrchestrator&&) = delete;

    // Core events, from the core's I/O thread; handled on the orchestrator's worker thread so the
    // I/O thread never waits for the monitor or the encoder.
    void on_viewer_monitors(std::span<const osplay_monitor> monitors);
    void on_keyframe_request(std::uint8_t stream_id);
    /// The viewer stopped the stream (e.g. its monitor was unplugged): stop capturing and remove
    /// the monitor; the session stays up.
    void on_stream_stopped(std::uint8_t stream_id);
    /// The session ended: stop capturing and remove the monitor.
    void on_session_closed();

    /// Blocks until queued work has run (tests).
    void wait_idle();

    /// `bits_per_pixel` is clamped to kMinBitsPerPixel...kMaxBitsPerPixel.
    static osplay_stream_config stream_for(const osplay_monitor& monitor,
                                           double bits_per_pixel = kBitsPerPixel);

private:
    void post(std::function<void()> task);
    void run();
    void start(const osplay_monitor& monitor);
    void stop_streaming();
    void fail(HostFailure failure, std::uint32_t detail);

    void on_access_unit(std::span<const std::uint8_t> annexb, bool keyframe,
                        std::uint64_t capture_time_us) override;
    void on_cursor_shape(std::uint32_t shape_id, const CursorImage& image) override;
    void on_cursor_position(bool visible, std::uint32_t shape_id, std::int32_t x, std::int32_t y,
                            std::uint64_t capture_time_us) override;
    void on_pipeline_failed(PipelineError error) override;

    HostCorePort* core_;               // never null; outlives the orchestrator
    VirtualDisplayProvider* displays_; // never null
    StreamPipelineFactory* pipelines_; // never null
    HostObserver* observer_;           // never null
    std::unique_ptr<StreamDump> dump_;
    double bits_per_pixel_;

    std::unique_ptr<StreamPipeline> pipeline_; // worker thread only
    bool monitor_added_ = false;               // worker thread only
    std::atomic<bool> failed_{false};

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> tasks_;
    bool busy_ = false;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace osplay::engine
