#include "media/frame_pacer.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using osplay::engine::FramePacer;

/// Feeds images every `input_interval` µs for one second; returns how many were encoded.
int encoded_per_second(std::int64_t input_interval) {
    FramePacer pacer(16'667);
    int encoded = 0;
    for (std::int64_t now = 0; now < 1'000'000; now += 100) {
        if (now % input_interval == 0) {
            pacer.on_new_image();
        }
        if (pacer.should_encode(now, false)) {
            pacer.encoded(now);
            ++encoded;
        }
    }
    return encoded;
}

TEST(FramePacer, FasterScreensAreCappedAtTheStreamRate) {
    EXPECT_NEAR(encoded_per_second(6'900), 60, 1) << "144 Hz composition";
    EXPECT_NEAR(encoded_per_second(10'000), 60, 1) << "100 Hz composition";
}

TEST(FramePacer, AScreenAtTheStreamRateKeepsEveryFrame) {
    EXPECT_NEAR(encoded_per_second(16'700), 60, 1);
}

TEST(FramePacer, SlowerScreensAreNotHeldBack) {
    EXPECT_EQ(encoded_per_second(50'000), 20);
}

TEST(FramePacer, KeyframesAreEncodedAtOnce) {
    FramePacer pacer(16'667);
    pacer.on_new_image();
    ASSERT_TRUE(pacer.should_encode(0, false));
    pacer.encoded(0);
    pacer.on_new_image();
    EXPECT_FALSE(pacer.should_encode(1'000, false));
    EXPECT_TRUE(pacer.should_encode(1'000, true));
}

TEST(FramePacer, WaitsOnlyForAPendingImage) {
    FramePacer pacer(16'667);
    EXPECT_FALSE(pacer.wait(0).has_value());
    pacer.on_new_image();
    pacer.encoded(0);
    pacer.on_new_image();
    EXPECT_EQ(pacer.wait(10'000), 6'667);
    EXPECT_EQ(pacer.wait(20'000), 0);
}

TEST(FramePacer, AnIdleScreenRestartsTheCadence) {
    FramePacer pacer(16'667);
    pacer.on_new_image();
    pacer.encoded(0);
    pacer.on_new_image(); // after five idle seconds
    ASSERT_TRUE(pacer.should_encode(5'000'000, false));
    pacer.encoded(5'000'000);
    pacer.on_new_image();
    EXPECT_FALSE(pacer.should_encode(5'000'001, false)) << "no burst to catch up";
    EXPECT_TRUE(pacer.should_encode(5'016'667, false));
}

} // namespace
