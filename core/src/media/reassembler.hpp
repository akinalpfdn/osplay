#pragma once

// Viewer-side frame reassembly for one stream (DEVPLAN "Media behavior"): no jitter buffer —
// frames are delivered the moment they are complete and in order. A missing fragment is NACKed
// once halfway through the frame interval; a frame still incomplete at its deadline (frame
// interval plus RTT) is dropped, and since later frames reference it, everything up to the next
// keyframe is discarded and a keyframe is requested.

#include "protocol/media_packets.hpp"
#include "util/time.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace osplay::media {

struct CompletedFrame {
    std::uint8_t stream_id = 0;
    std::uint32_t frame_id = 0;
    bool keyframe = false;
    bool codec_config = false;
    std::uint64_t capture_time_us = 0;
    std::span<const std::uint8_t> data; // valid only during the callback
};

/// Receives the reassembler's decisions.
class ReassemblySink {
public:
    virtual ~ReassemblySink() = default;
    virtual void on_frame(const CompletedFrame& frame) = 0;
    virtual void on_nack(const protocol::Nack& nack) = 0;
    /// Called on every tick while the stream waits for a keyframe; the caller rate-limits requests.
    virtual void on_keyframe_needed() = 0;
};

struct ReassemblerConfig {
    Microseconds frame_interval{16'667};
    Microseconds minimum_rtt{2'000};
    std::size_t max_frames_in_flight = 8;
};

struct ReassemblerStats {
    std::uint64_t frames_completed = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t fragments_received = 0;
    std::uint64_t fragments_ignored = 0; // stale, duplicate or inconsistent
    std::uint64_t nacks_sent = 0;
};

class Reassembler {
public:
    Reassembler(std::uint8_t stream_id, ReassemblerConfig config, ReassemblySink& sink);

    void add(const protocol::VideoFragment& fragment, TimePoint now);
    void tick(TimePoint now);
    void set_rtt(Microseconds rtt) { rtt_ = rtt; }
    /// Forget everything and wait for a keyframe (stream restarted or resumed).
    void reset();

    [[nodiscard]] const ReassemblerStats& stats() const { return stats_; }
    [[nodiscard]] bool waiting_for_keyframe() const { return waiting_for_keyframe_; }

private:
    struct Pending {
        /// Last time this frame made progress (a new fragment, or the gap was found). NACK and
        /// deadline count from here, so a large frame still arriving is not given up.
        TimePoint last_progress;
        std::uint16_t fragment_count = 0; // 0 while only a gap placeholder
        std::uint16_t fragments_received = 0;
        std::vector<bool> have;
        std::vector<std::uint8_t> data;
        std::size_t size = 0; // known once the last fragment arrived
        bool keyframe = false;
        bool codec_config = false;
        std::uint64_t capture_time_us = 0;
        bool nacked = false;

        [[nodiscard]] bool complete() const {
            return fragment_count > 0 && fragments_received == fragment_count;
        }
    };

    void deliver_ready();
    void drop_through(std::uint32_t frame_id);
    void send_nack(std::uint32_t frame_id, const Pending& frame);
    [[nodiscard]] Microseconds deadline() const;

    std::uint8_t stream_id_;
    ReassemblerConfig config_;
    ReassemblySink* sink_; // never null; must outlive the reassembler
    std::map<std::uint32_t, Pending> pending_;
    std::optional<std::uint32_t> next_frame_id_; // the next frame to deliver
    bool waiting_for_keyframe_ = true;
    Microseconds rtt_{0};
    ReassemblerStats stats_;
};

} // namespace osplay::media
