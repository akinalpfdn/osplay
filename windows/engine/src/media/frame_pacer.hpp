#pragma once

// Desktop Duplication delivers a frame whenever the screen changes, which can be faster than the
// stream's refresh rate (DWM composes at the fastest monitor's rate). The pacer encodes the newest
// image once per frame interval, on a fixed cadence, and keyframe requests at once. Times are in
// any monotonic unit (performance-counter counts on Windows). Portable and unit tested.

#include <cstdint>
#include <optional>

namespace osplay::engine {

class FramePacer {
public:
    explicit FramePacer(std::int64_t interval) : interval_(interval > 0 ? interval : 1) {}

    /// A new screen image was captured (it replaces any image not yet encoded).
    void on_new_image() { pending_ = true; }

    /// Whether to encode now; a keyframe request encodes the newest image regardless of cadence.
    [[nodiscard]] bool should_encode(std::int64_t now, bool keyframe) const {
        return keyframe || (pending_ && now >= next_due_);
    }

    /// Call after encoding at `now`.
    void encoded(std::int64_t now) {
        pending_ = false;
        // Fell behind (idle screen, stall): restart the cadence from now instead of bursting.
        if (now - next_due_ >= interval_) {
            next_due_ = now;
        }
        next_due_ += interval_;
    }

    /// How long until a waiting image is due; nothing if no image waits.
    [[nodiscard]] std::optional<std::int64_t> wait(std::int64_t now) const {
        if (!pending_) {
            return std::nullopt;
        }
        return next_due_ > now ? next_due_ - now : 0;
    }

private:
    std::int64_t interval_;
    std::int64_t next_due_ = 0;
    bool pending_ = false;
};

} // namespace osplay::engine
