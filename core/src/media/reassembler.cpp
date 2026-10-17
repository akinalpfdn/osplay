#include "media/reassembler.hpp"

#include <algorithm>

namespace osplay::media {

using protocol::kMaxFragmentsPerFrame;
using protocol::kMaxNackRanges;
using protocol::kMaxVideoPayload;

Reassembler::Reassembler(std::uint8_t stream_id, ReassemblerConfig config, ReassemblySink& sink)
    : stream_id_(stream_id), config_(config), sink_(&sink) {}

Microseconds Reassembler::deadline() const {
    return config_.frame_interval + std::max(rtt_, config_.minimum_rtt);
}

void Reassembler::reset() {
    pending_.clear();
    next_frame_id_.reset();
    waiting_for_keyframe_ = true;
}

void Reassembler::add(const protocol::VideoFragment& fragment, TimePoint now) {
    ++stats_.fragments_received;
    const bool keyframe = (fragment.flags & protocol::kVideoKeyframe) != 0;
    const bool stale = next_frame_id_ && fragment.frame_id < *next_frame_id_;
    if (fragment.stream_id != stream_id_ || stale) {
        ++stats_.fragments_ignored;
        return;
    }
    if (waiting_for_keyframe_ && !keyframe) {
        // Useless unless it follows a keyframe that is still being received.
        const bool after_pending_keyframe = std::ranges::any_of(pending_, [&](const auto& entry) {
            return entry.second.keyframe && entry.first < fragment.frame_id;
        });
        if (!after_pending_keyframe) {
            ++stats_.fragments_ignored;
            return;
        }
    }

    auto [it, inserted] = pending_.try_emplace(fragment.frame_id);
    Pending& frame = it->second;
    if (inserted) {
        frame.first_seen = now;
    }
    if (frame.fragment_count == 0) {
        frame.fragment_count = fragment.fragment_count;
        frame.have.assign(fragment.fragment_count, false);
        frame.data.resize(std::size_t{fragment.fragment_count} * kMaxVideoPayload);
    }
    if (frame.fragment_count != fragment.fragment_count || frame.have[fragment.fragment_index]) {
        ++stats_.fragments_ignored; // inconsistent or duplicate (e.g. a retransmission that raced)
        return;
    }

    const std::size_t offset = std::size_t{fragment.fragment_index} * kMaxVideoPayload;
    std::ranges::copy(fragment.payload, frame.data.begin() + static_cast<std::ptrdiff_t>(offset));
    frame.have[fragment.fragment_index] = true;
    ++frame.fragments_received;
    if (fragment.fragment_index + 1 == fragment.fragment_count) {
        frame.size = offset + fragment.payload.size();
    }
    frame.keyframe = keyframe;
    frame.codec_config = (fragment.flags & protocol::kVideoCodecConfig) != 0;
    frame.capture_time_us = fragment.capture_time_us;

    // Frames between the next expected one and this one have not been seen at all yet.
    if (!waiting_for_keyframe_ && next_frame_id_ && fragment.frame_id > *next_frame_id_) {
        if (fragment.frame_id - *next_frame_id_ > config_.max_frames_in_flight) {
            drop_through(fragment.frame_id - 1);
        } else {
            for (std::uint32_t id = *next_frame_id_; id < fragment.frame_id; ++id) {
                pending_.try_emplace(id, Pending{.first_seen = now});
            }
        }
    }
    deliver_ready();
}

void Reassembler::deliver_ready() {
    if (waiting_for_keyframe_) {
        const auto keyframe = std::ranges::find_if(pending_, [](const auto& entry) {
            return entry.second.keyframe && entry.second.complete();
        });
        if (keyframe == pending_.end()) {
            return;
        }
        const std::uint32_t keyframe_id = keyframe->first;
        for (auto it = pending_.begin(); it != pending_.end() && it->first < keyframe_id;) {
            ++stats_.frames_dropped;
            it = pending_.erase(it);
        }
        next_frame_id_ = keyframe_id;
        waiting_for_keyframe_ = false;
    }
    while (next_frame_id_) {
        const auto it = pending_.find(*next_frame_id_);
        if (it == pending_.end() || !it->second.complete()) {
            return;
        }
        const Pending& frame = it->second;
        sink_->on_frame(CompletedFrame{
            .stream_id = stream_id_,
            .frame_id = it->first,
            .keyframe = frame.keyframe,
            .codec_config = frame.codec_config,
            .capture_time_us = frame.capture_time_us,
            .data = std::span(frame.data).first(frame.size),
        });
        ++stats_.frames_completed;
        pending_.erase(it);
        ++*next_frame_id_;
    }
}

void Reassembler::drop_through(std::uint32_t frame_id) {
    for (auto it = pending_.begin(); it != pending_.end() && it->first <= frame_id;) {
        ++stats_.frames_dropped;
        it = pending_.erase(it);
    }
    next_frame_id_ = frame_id + 1;
    waiting_for_keyframe_ = true;
}

void Reassembler::send_nack(std::uint32_t frame_id, const Pending& frame) {
    protocol::Nack nack{.stream_id = stream_id_, .frame_id = frame_id, .ranges = {}};
    if (frame.fragment_count == 0) {
        // Nothing of this frame arrived, so its size is unknown: ask for every fragment.
        nack.ranges.push_back(
            {.first_fragment = 0, .count = static_cast<std::uint16_t>(kMaxFragmentsPerFrame)});
    } else {
        std::uint16_t index = 0;
        while (index < frame.fragment_count && nack.ranges.size() < kMaxNackRanges) {
            if (frame.have[index]) {
                ++index;
                continue;
            }
            const std::uint16_t first = index;
            // The last range absorbs whatever does not fit into kMaxNackRanges ranges.
            const bool last_range = nack.ranges.size() + 1 == kMaxNackRanges;
            while (index < frame.fragment_count && (last_range || !frame.have[index])) {
                ++index;
            }
            nack.ranges.push_back(
                {.first_fragment = first, .count = static_cast<std::uint16_t>(index - first)});
        }
    }
    ++stats_.nacks_sent;
    sink_->on_nack(nack);
}

void Reassembler::tick(TimePoint now) {
    while (pending_.size() > config_.max_frames_in_flight) {
        drop_through(pending_.begin()->first);
    }

    bool dropped = true;
    while (dropped) {
        dropped = false;
        for (auto& [frame_id, frame] : pending_) {
            if (frame.complete()) {
                continue;
            }
            const auto elapsed = std::chrono::duration_cast<Microseconds>(now - frame.first_seen);
            if (elapsed >= deadline()) {
                drop_through(frame_id);
                deliver_ready(); // a newer keyframe may already be complete
                dropped = true;
                break;
            }
            const bool useful = !waiting_for_keyframe_ || frame.keyframe;
            if (!frame.nacked && useful && elapsed >= config_.frame_interval / 2) {
                frame.nacked = true;
                send_nack(frame_id, frame);
            }
        }
    }

    if (waiting_for_keyframe_) {
        sink_->on_keyframe_needed();
    }
}

} // namespace osplay::media
