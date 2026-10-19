#pragma once

// One received stream on Windows: decode (Media Foundation) and present (flip-model swap chain
// covering one monitor), or a fake in tests. The orchestrator only sees this interface.

#include "host/stream_pipeline.hpp"

#include <osplay/osplay.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace osplay::engine {

/// A monitor this viewer offers, as the platform found it.
struct ViewerMonitor {
    std::uint8_t id = 0;
    MonitorRect rect;
    osplay_monitor description{}; ///< `name` is not used; the orchestrator never forwards it
};

struct ViewerStreamStats {
    std::uint64_t frames_presented = 0;
    std::uint64_t decode_errors = 0;
};

/// Told about decoder trouble, from the stream's own thread.
class ViewerStreamSink {
public:
    virtual ~ViewerStreamSink() = default;
    /// The decoder needs a keyframe (start, or after an error).
    virtual void on_keyframe_needed(std::uint8_t stream_id) = 0;
};

class ViewerStream {
public:
    virtual ~ViewerStream() = default;
    /// Copies the access unit and decodes it on the stream's thread; never blocks.
    virtual void submit(std::span<const std::uint8_t> annexb, bool keyframe,
                        std::uint64_t capture_time_us) = 0;
    virtual void set_visible(bool visible) = 0;
    /// One line of UTF-8 drawn over the picture; empty hides it. Never blocks.
    virtual void set_overlay(const std::string& text) = 0;
    [[nodiscard]] virtual ViewerStreamStats stats() const = 0;
};

class ViewerStreamFactory {
public:
    virtual ~ViewerStreamFactory() = default;
    /// Opens a presentation window covering `monitor` and a decoder for `stream`.
    [[nodiscard]] virtual win::Expected<std::unique_ptr<ViewerStream>, PipelineError>
    start(const osplay_stream_config& stream, const ViewerMonitor& monitor, bool visible,
          ViewerStreamSink& sink) = 0;
};

} // namespace osplay::engine
