#include "display/dpi_scale.hpp"

#include <gtest/gtest.h>

using osplay::engine::DpiScaleRange;
using osplay::engine::relative_scale_step;

TEST(DpiScale, AViewersScaleBecomesStepsFromTheRecommendation) {
    // Recommended 150% (100% is two steps down), up to 300% (four steps up).
    const DpiScaleRange range{.min_relative = -2, .current_relative = 0, .max_relative = 4};

    EXPECT_EQ(relative_scale_step(100, range), -2);
    EXPECT_EQ(relative_scale_step(150, range), 0);
    EXPECT_EQ(relative_scale_step(200, range), 2);
}

TEST(DpiScale, ScalesBetweenStepsTakeTheNearest) {
    const DpiScaleRange range{.min_relative = 0, .current_relative = 0, .max_relative = 8};

    EXPECT_EQ(relative_scale_step(133, range), 1) << "125%";
    EXPECT_EQ(relative_scale_step(140, range), 2) << "150%";
    EXPECT_EQ(relative_scale_step(280, range), 7) << "300%";
}

TEST(DpiScale, AScaleAboveTheMonitorsMaximumStopsAtIt) {
    const DpiScaleRange range{.min_relative = 0, .current_relative = 0, .max_relative = 2};

    EXPECT_EQ(relative_scale_step(400, range), 2) << "150% is this monitor's maximum";
}

TEST(DpiScale, ARangeBeyondTheKnownStepsIsRefused) {
    EXPECT_EQ(
        relative_scale_step(200, {.min_relative = 1, .current_relative = 0, .max_relative = 0}),
        std::nullopt);
    EXPECT_EQ(
        relative_scale_step(200, {.min_relative = -4, .current_relative = 0, .max_relative = 9}),
        std::nullopt);
}
