#pragma once

#include "util/time.hpp"

#include <optional>

namespace osplay::media {

/// Allows an action at most once per interval (keyframe requests, DEVPLAN "IDR requests are
/// rate-limited per stream").
class RateLimiter {
public:
    explicit RateLimiter(Microseconds interval) : interval_(interval) {}

    [[nodiscard]] bool allow(TimePoint now) {
        if (last_ && now - *last_ < interval_) {
            return false;
        }
        last_ = now;
        return true;
    }

private:
    Microseconds interval_;
    std::optional<TimePoint> last_;
};

} // namespace osplay::media
