#include "session/clock_offset.hpp"

#include <gtest/gtest.h>

namespace {

using osplay::session::ClockOffsetEstimator;

TEST(ClockOffsetEstimator, HasNoEstimateBeforeTheFirstExchange) {
    const ClockOffsetEstimator estimator;
    EXPECT_FALSE(estimator.offset_us().has_value());
}

TEST(ClockOffsetEstimator, ADelayedReplyDoesNotMoveTheEstimate) {
    ClockOffsetEstimator estimator;
    estimator.add(4'000, 1'000);
    estimator.add(5'000, 1'200);
    // A Wi-Fi stall: three seconds round trip, the reply half of it late.
    estimator.add(3'000'000, 1'500'000);
    EXPECT_EQ(estimator.offset_us(), 1'000);
}

TEST(ClockOffsetEstimator, FollowsTheFastestRecentExchange) {
    ClockOffsetEstimator estimator;
    estimator.add(2'000, 1'000);
    // The fast sample leaves the window; the fastest of the remaining ones decides.
    for (std::size_t i = 0; i < ClockOffsetEstimator::kWindow - 1; ++i) {
        estimator.add(6'000, 9'000);
    }
    estimator.add(5'000, 4'000);
    EXPECT_EQ(estimator.offset_us(), 4'000);
}

} // namespace
