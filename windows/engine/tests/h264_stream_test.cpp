#include "media/h264_stream.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using osplay::engine::nal_units;
using osplay::engine::ParameterSetCache;
using Bytes = std::vector<std::uint8_t>;

TEST(H264Stream, SplitsAnnexB) {
    const Bytes stream{0, 0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2, 0, 0, 0, 1, 0x65, 3};
    const auto units = nal_units(stream);
    ASSERT_EQ(units.size(), 3U);
    EXPECT_EQ(Bytes(units[2].begin(), units[2].end()), (Bytes{0x65, 3}));
    EXPECT_TRUE(nal_units(Bytes{1, 2, 3}).empty());
}

TEST(H264Stream, KeyframesWithoutParameterSetsGetTheCachedOnes) {
    ParameterSetCache cache;
    cache.remember(Bytes{0, 0, 0, 1, 0x67, 0xAA, 0, 0, 0, 1, 0x68, 0xBB});
    bool keyframe = false;
    const auto idr = cache.prepare(Bytes{0, 0, 0, 1, 0x65, 1, 2}, keyframe);
    EXPECT_TRUE(keyframe);
    EXPECT_EQ(idr, (Bytes{0, 0, 0, 1, 0x67, 0xAA, 0, 0, 0, 1, 0x68, 0xBB, 0, 0, 0, 1, 0x65, 1, 2}));

    const auto p = cache.prepare(Bytes{0, 0, 1, 0x41, 9}, keyframe);
    EXPECT_FALSE(keyframe);
    EXPECT_EQ(p, (Bytes{0, 0, 0, 1, 0x41, 9})) << "start codes are normalized to four bytes";
}

TEST(H264Stream, KeyframesThatCarryParameterSetsAreLeftAloneAndUpdateTheCache) {
    ParameterSetCache cache;
    bool keyframe = false;
    const Bytes with_sets{0, 0, 0, 1, 0x67, 0x11, 0, 0, 0, 1, 0x68, 0x22, 0, 0, 0, 1, 0x65, 5};
    EXPECT_EQ(cache.prepare(with_sets, keyframe), with_sets);
    EXPECT_TRUE(keyframe);
    const auto next = cache.prepare(Bytes{0, 0, 0, 1, 0x65, 6}, keyframe);
    EXPECT_EQ(next, (Bytes{0, 0, 0, 1, 0x67, 0x11, 0, 0, 0, 1, 0x68, 0x22, 0, 0, 0, 1, 0x65, 6}));
}

} // namespace
