#include "media/cursor.hpp"
#include "media/packetizer.hpp"
#include "media/rate_limiter.hpp"
#include "media/reassembler.hpp"
#include "media/retransmit_cache.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace {

namespace media = osplay::media;
namespace protocol = osplay::protocol;
using osplay::Microseconds;
using osplay::TimePoint;
using std::chrono::microseconds;
using std::chrono::milliseconds;

std::vector<std::uint8_t> frame_bytes(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(seed + i * 13);
    }
    return bytes;
}

media::AccessUnit unit(const std::vector<std::uint8_t>& data, bool keyframe) {
    return {.stream_id = 0,
            .keyframe = keyframe,
            .codec_config = keyframe,
            .capture_time_us = 42,
            .data = data};
}

// --- Packetizer ----------------------------------------------------------------------------------

TEST(Packetizer, SplitsIntoFullFragmentsAndNumbersFrames) {
    media::Packetizer packetizer;
    const auto data = frame_bytes(3000, 1);
    std::vector<protocol::VideoFragment> fragments;
    ASSERT_EQ(packetizer.packetize(unit(data, true), fragments).value(), 0U);
    ASSERT_EQ(fragments.size(), 3U);
    EXPECT_EQ(fragments[0].payload.size(), protocol::kMaxVideoPayload);
    EXPECT_EQ(fragments[2].payload.size(), 3000 - 2 * protocol::kMaxVideoPayload);
    EXPECT_EQ(fragments[1].flags, protocol::kVideoKeyframe | protocol::kVideoCodecConfig);
    EXPECT_EQ(fragments[2].fragment_count, 3);
    EXPECT_EQ(packetizer.packetize(unit(data, false), fragments).value(), 1U);
    EXPECT_EQ(fragments[0].flags, 0);
}

TEST(Packetizer, RetransmissionSubsetIsClamped) {
    const auto data = frame_bytes(3000, 1);
    std::vector<protocol::VideoFragment> fragments;
    media::Packetizer::fragments(7, unit(data, false), 1, 4096, fragments);
    ASSERT_EQ(fragments.size(), 2U);
    EXPECT_EQ(fragments[0].fragment_index, 1);
    EXPECT_EQ(fragments[0].frame_id, 7U);
}

TEST(Packetizer, RejectsInvalidUnits) {
    media::Packetizer packetizer;
    std::vector<protocol::VideoFragment> fragments;
    const std::vector<std::uint8_t> empty;
    EXPECT_EQ(packetizer.packetize(unit(empty, true), fragments).error(),
              osplay::Error::InvalidArgument);
    const auto data = frame_bytes(10, 1);
    media::AccessUnit config_without_keyframe = unit(data, false);
    config_without_keyframe.codec_config = true;
    EXPECT_EQ(packetizer.packetize(config_without_keyframe, fragments).error(),
              osplay::Error::InvalidArgument);
    const std::vector<std::uint8_t> huge(4096 * protocol::kMaxVideoPayload + 1);
    EXPECT_EQ(packetizer.packetize(unit(huge, true), fragments).error(), osplay::Error::TooLarge);
}

// --- RetransmitCache and RateLimiter
// ---------------------------------------------------------------

TEST(RetransmitCache, KeepsRecentFramesWithinBudget) {
    media::RetransmitCache cache(3, 10'000);
    const auto data = frame_bytes(4000, 1);
    for (std::uint32_t id = 0; id < 4; ++id) {
        cache.store(id, unit(data, id == 0));
    }
    EXPECT_EQ(cache.find(0), nullptr) << "evicted by frame count";
    EXPECT_EQ(cache.find(1), nullptr) << "evicted by bytes: 3 x 4000 > 10000";
    ASSERT_NE(cache.find(3), nullptr);
    EXPECT_TRUE(std::ranges::equal(cache.find(3)->data, data));
}

TEST(RateLimiter, AllowsOncePerInterval) {
    media::RateLimiter limiter(Microseconds{250'000});
    const TimePoint start{};
    EXPECT_TRUE(limiter.allow(start));
    EXPECT_FALSE(limiter.allow(start + milliseconds(100)));
    EXPECT_TRUE(limiter.allow(start + milliseconds(250)));
}

// --- Reassembler ---------------------------------------------------------------------------------

class RecordingSink final : public media::ReassemblySink {
public:
    std::vector<std::uint32_t> frame_ids;
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<protocol::Nack> nacks;
    int keyframe_requests = 0;

    void on_frame(const media::CompletedFrame& frame) override {
        frame_ids.push_back(frame.frame_id);
        frames.emplace_back(frame.data.begin(), frame.data.end());
    }
    void on_nack(const protocol::Nack& nack) override { nacks.push_back(nack); }
    void on_keyframe_needed() override { ++keyframe_requests; }
};

struct ReassemblerTest : ::testing::Test {
    RecordingSink sink;
    media::Reassembler reassembler{0, {.frame_interval = Microseconds{16'000}}, sink};
    media::Packetizer packetizer;
    std::vector<std::vector<std::uint8_t>> sent;
    TimePoint now{};

    /// Packetizes a frame and returns its fragments (views into `sent`).
    std::vector<protocol::VideoFragment> make(std::size_t size, bool keyframe) {
        sent.push_back(frame_bytes(size, static_cast<std::uint8_t>(sent.size())));
        std::vector<protocol::VideoFragment> fragments;
        const auto id = packetizer.packetize(unit(sent.back(), keyframe), fragments);
        EXPECT_TRUE(id.has_value());
        return fragments;
    }
    void feed(const std::vector<protocol::VideoFragment>& fragments) {
        for (const auto& fragment : fragments) {
            reassembler.add(fragment, now);
        }
    }
};

TEST_F(ReassemblerTest, DeliversCompleteFramesInOrder) {
    feed(make(3000, true));
    feed(make(500, false));
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0, 1}));
    EXPECT_EQ(sink.frames[0], sent[0]);
    EXPECT_EQ(sink.frames[1], sent[1]);
    EXPECT_FALSE(reassembler.waiting_for_keyframe());
}

TEST_F(ReassemblerTest, WaitsForAKeyframeFirst) {
    feed(make(500, false));
    reassembler.tick(now);
    EXPECT_TRUE(sink.frame_ids.empty());
    EXPECT_EQ(sink.keyframe_requests, 1);
    feed(make(500, true));
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{1}));
}

TEST_F(ReassemblerTest, ReordersFragmentsAndHoldsAFrameUntilItsPredecessorCompletes) {
    feed(make(500, true));
    auto first = make(3000, false);
    auto second = make(500, false);
    feed({first[2], first[0]});
    feed(second);
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0})) << "frame 2 waits for frame 1";
    feed({first[1]});
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0, 1, 2}));
    EXPECT_EQ(sink.frames[1], sent[1]);
}

TEST_F(ReassemblerTest, NacksMissingFragmentsOnceAndRecoversWithTheRetransmission) {
    feed(make(500, true));
    auto frame = make(5 * protocol::kMaxVideoPayload, false);
    feed({frame[0], frame[2], frame[4]});
    now += milliseconds(7);
    reassembler.tick(now);
    EXPECT_TRUE(sink.nacks.empty()) << "not yet half a frame interval";
    now += milliseconds(2);
    reassembler.tick(now);
    ASSERT_EQ(sink.nacks.size(), 1U);
    EXPECT_EQ(sink.nacks[0].frame_id, 1U);
    EXPECT_EQ(sink.nacks[0].ranges,
              (std::vector<protocol::NackRange>{{.first_fragment = 1, .count = 1},
                                                {.first_fragment = 3, .count = 1}}));
    reassembler.tick(now + milliseconds(1));
    EXPECT_EQ(sink.nacks.size(), 1U) << "a frame is NACKed only once";
    feed({frame[1], frame[3]});
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0, 1}));
}

TEST_F(ReassemblerTest, DropsAtTheDeadlineAndSkipsToTheNextKeyframe) {
    feed(make(500, true));
    auto lost = make(3000, false);
    feed({lost[0], lost[1]});
    auto dependent = make(500, false);
    feed(dependent);
    now += milliseconds(19); // frame interval 16 ms + minimum RTT 2 ms
    reassembler.tick(now);
    EXPECT_TRUE(reassembler.waiting_for_keyframe());
    EXPECT_GE(sink.keyframe_requests, 1);
    feed(make(500, false));
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0}))
        << "P-frames after a loss are useless";
    feed(make(500, true));
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0, 4}));
    EXPECT_GE(reassembler.stats().frames_dropped, 2U);
}

TEST_F(ReassemblerTest, KeepsAFrameThatIsStillArrivingPastTheDeadline) {
    feed(make(500, true));
    const auto large = make(30 * protocol::kMaxVideoPayload, false);
    for (const auto& fragment : large) {
        reassembler.add(fragment, now);
        now += milliseconds(2); // 60 ms in total, far beyond one frame interval
        reassembler.tick(now);
    }
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0, 1}));
    EXPECT_TRUE(sink.nacks.empty());
    EXPECT_EQ(sink.keyframe_requests, 0);
}

TEST_F(ReassemblerTest, AStallOfThisThreadIsNotCountedAgainstAFrame) {
    feed(make(500, true));
    reassembler.tick(now);
    const auto frame = make(3000, false);
    feed({frame[0]});
    // No tick for 30 ms: the thread did not run, and the rest of the frame waits in the socket.
    now += milliseconds(30);
    reassembler.tick(now);
    feed({frame[1], frame[2]});
    EXPECT_EQ(sink.frame_ids, (std::vector<std::uint32_t>{0, 1}));
    EXPECT_EQ(reassembler.stats().frames_dropped, 0U);
}

TEST_F(ReassemblerTest, ACoarseTimerStillNacksAndDropsInTime) {
    feed(make(500, true));
    reassembler.tick(now);
    auto frame = make(3000, false);
    feed({frame[0]});
    // Windows' default timer resolution: ticks 15.6 ms apart instead of 2 ms.
    for (int tick = 0; tick < 2; ++tick) {
        now += microseconds(15'625);
        reassembler.tick(now);
    }
    ASSERT_EQ(sink.nacks.size(), 1U) << "NACKed within two coarse ticks";
    for (int tick = 0; tick < 3; ++tick) {
        now += microseconds(15'625);
        reassembler.tick(now);
    }
    EXPECT_TRUE(reassembler.waiting_for_keyframe()) << "dropped, not held until 8 frames pile up";
}

TEST_F(ReassemblerTest, NacksWholeFramesThatNeverArrived) {
    feed(make(500, true));
    make(500, false); // frame 1: lost entirely
    feed(make(500, false));
    now += milliseconds(9);
    reassembler.tick(now);
    ASSERT_EQ(sink.nacks.size(), 1U);
    EXPECT_EQ(sink.nacks[0].frame_id, 1U);
    EXPECT_EQ(sink.nacks[0].ranges,
              (std::vector<protocol::NackRange>{{.first_fragment = 0, .count = 4096}}));
}

TEST_F(ReassemblerTest, IgnoresDuplicatesAndStaleFragments) {
    auto keyframe = make(500, true);
    feed(keyframe);
    feed(keyframe);
    EXPECT_EQ(sink.frame_ids.size(), 1U);
    EXPECT_EQ(reassembler.stats().fragments_ignored, 1U);
}

TEST_F(ReassemblerTest, TooManyFramesInFlightForceAKeyframe) {
    feed(make(500, true));
    auto stuck = make(3000, false);
    feed({stuck[0]});
    for (int i = 0; i < 9; ++i) {
        feed(make(500, false));
    }
    reassembler.tick(now);
    EXPECT_TRUE(reassembler.waiting_for_keyframe());
}

// --- Cursor shapes
// ---------------------------------------------------------------------------------

media::CursorShape shape(std::uint32_t id, std::uint16_t width, std::uint16_t height,
                         const std::vector<std::uint8_t>& bgra) {
    return {.stream_id = 1,
            .shape_id = id,
            .width = width,
            .height = height,
            .hotspot_x = 1,
            .hotspot_y = 2,
            .bgra = bgra};
}

TEST(CursorShape, SplitsAndReassemblesOutOfOrder) {
    const auto bgra = frame_bytes(std::size_t{48} * 48 * 4, 3);
    std::vector<protocol::CursorShapeFragment> fragments;
    ASSERT_TRUE(media::split_cursor_shape(shape(5, 48, 48, bgra), fragments).has_value());
    ASSERT_EQ(fragments.size(), 8U); // 9216 bytes / 1155
    EXPECT_EQ(fragments.back().payload.size(), 9216 - 7 * protocol::kMaxCursorShapePayload);

    media::CursorShapeAssembler assembler;
    std::optional<media::CursorShape> completed;
    for (std::size_t i = fragments.size(); i-- > 0;) {
        EXPECT_FALSE(completed.has_value());
        completed = assembler.add(fragments[i]);
    }
    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->shape_id, 5U);
    EXPECT_EQ(completed->hotspot_y, 2);
    EXPECT_TRUE(std::ranges::equal(completed->bgra, bgra));
    EXPECT_FALSE(assembler.add(fragments[0]).has_value()) << "a resent shape is delivered once";
}

TEST(CursorShape, ANewerShapeAbandonsTheOneInProgress) {
    const auto first = frame_bytes(std::size_t{32} * 32 * 4, 1);
    const auto second = frame_bytes(std::size_t{16} * 16 * 4, 2);
    std::vector<protocol::CursorShapeFragment> old_fragments;
    std::vector<protocol::CursorShapeFragment> new_fragments;
    ASSERT_TRUE(media::split_cursor_shape(shape(1, 32, 32, first), old_fragments).has_value());
    ASSERT_TRUE(media::split_cursor_shape(shape(2, 16, 16, second), new_fragments).has_value());

    media::CursorShapeAssembler assembler;
    EXPECT_FALSE(assembler.add(old_fragments[0]).has_value());
    std::optional<media::CursorShape> completed;
    for (const auto& fragment : new_fragments) {
        completed = assembler.add(fragment);
    }
    ASSERT_TRUE(completed.has_value());
    EXPECT_EQ(completed->shape_id, 2U);
}

TEST(CursorShape, InconsistentFragmentsAreIgnored) {
    const auto bgra = frame_bytes(std::size_t{32} * 32 * 4, 1);
    std::vector<protocol::CursorShapeFragment> fragments;
    ASSERT_TRUE(media::split_cursor_shape(shape(1, 32, 32, bgra), fragments).has_value());
    media::CursorShapeAssembler assembler;
    EXPECT_FALSE(assembler.add(fragments[0]).has_value());

    auto resized = fragments[1];
    resized.width = 31;
    EXPECT_FALSE(assembler.add(resized).has_value());
    auto short_payload = fragments[1];
    short_payload.payload = short_payload.payload.first(10);
    EXPECT_FALSE(assembler.add(short_payload).has_value());
    EXPECT_FALSE(assembler.add(fragments[0]).has_value()) << "duplicate";

    std::optional<media::CursorShape> completed;
    for (std::size_t i = 1; i < fragments.size(); ++i) {
        completed = assembler.add(fragments[i]);
    }
    EXPECT_TRUE(completed.has_value());
}

TEST(CursorShape, RejectsInvalidImages) {
    std::vector<protocol::CursorShapeFragment> fragments;
    const auto bgra = frame_bytes(16, 0);
    EXPECT_EQ(media::split_cursor_shape(shape(1, 2, 3, bgra), fragments).error(),
              osplay::Error::InvalidArgument)
        << "size does not match";
    const auto big = frame_bytes(std::size_t{257} * 1 * 4, 0);
    EXPECT_EQ(media::split_cursor_shape(shape(1, 257, 1, big), fragments).error(),
              osplay::Error::InvalidArgument);
}

} // namespace
