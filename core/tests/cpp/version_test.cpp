#include <osplay/osplay.h>

#include <gtest/gtest.h>

namespace {

TEST(CoreVersion, LinkedLibraryMatchesHeader) {
    const osplay_version version = osplay_core_version();

    EXPECT_EQ(version.major, OSPLAY_CORE_VERSION_MAJOR);
    EXPECT_EQ(version.minor, OSPLAY_CORE_VERSION_MINOR);
    EXPECT_EQ(version.patch, OSPLAY_CORE_VERSION_PATCH);
}

} // namespace
