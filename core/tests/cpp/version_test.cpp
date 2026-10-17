#include <osplay/osplay.h>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace {

TEST(CoreVersion, LinkedLibraryMatchesHeader) {
    const osplay_version version = osplay_core_version();

    EXPECT_EQ(version.major, OSPLAY_CORE_VERSION_MAJOR);
    EXPECT_EQ(version.minor, OSPLAY_CORE_VERSION_MINOR);
    EXPECT_EQ(version.patch, OSPLAY_CORE_VERSION_PATCH);
}

TEST(CoreClock, IsMonotonicAndCountsMicroseconds) {
    const uint64_t before = osplay_clock_microseconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const uint64_t after = osplay_clock_microseconds();

    EXPECT_GE(after - before, 20'000U);
    EXPECT_LT(after - before, 2'000'000U);
}

} // namespace
