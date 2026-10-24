#include "session/bitrate_controller.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

namespace {

using osplay::session::BitrateController;
using osplay::session::BitrateTunables;

constexpr std::uint32_t kConfigured = 20'000;
constexpr std::uint64_t kFragments = 2'000; // fragments sent in one report interval

/// Feeds `count` identical reports and returns the last announced target, if any.
std::optional<std::uint32_t> feed(BitrateController& controller, int count,
                                  std::uint16_t loss_permille, std::uint64_t resent = 0) {
    std::optional<std::uint32_t> last;
    for (int i = 0; i < count; ++i) {
        if (const auto target = controller.on_report(loss_permille, kFragments, resent)) {
            last = target;
        }
    }
    return last;
}

TEST(BitrateController, LostFramesLowerTheBitrateDownToTheFloor) {
    BitrateController controller(kConfigured, BitrateTunables{});
    EXPECT_EQ(controller.on_report(30, kFragments, 0), 17'000U);
    EXPECT_EQ(controller.on_report(30, kFragments, 0), 14'450U);
    feed(controller, 20, 30);
    EXPECT_EQ(controller.target_kbps(), 5'000U) << "a quarter of the configured bitrate";
    EXPECT_FALSE(controller.on_report(30, kFragments, 0).has_value()) << "already at the floor";
}

TEST(BitrateController, ManyResentFragmentsCountAsCongestion) {
    BitrateController controller(kConfigured, BitrateTunables{});
    EXPECT_EQ(controller.on_report(0, kFragments, kFragments / 4), 17'000U);
    EXPECT_FALSE(controller.on_report(0, kFragments, kFragments / 10).has_value())
        << "10% resent is repaired in time: a clean interval";
}

TEST(BitrateController, ACleanLinkClimbsBackToTheConfiguredBitrateButNotAbove) {
    BitrateController controller(kConfigured, BitrateTunables{});
    feed(controller, 3, 50);
    const std::uint32_t lowered = controller.target_kbps();
    ASSERT_LT(lowered, kConfigured);
    EXPECT_FALSE(controller.on_report(0, kFragments, 0).has_value())
        << "one clean report is not enough";
    feed(controller, 200, 0);
    EXPECT_EQ(controller.target_kbps(), kConfigured);
    EXPECT_FALSE(feed(controller, 10, 0).has_value()) << "nothing to announce at the ceiling";
}

TEST(BitrateController, ALowConfiguredBitrateKeepsTheMinimumFloor) {
    BitrateController controller(1'200, BitrateTunables{});
    feed(controller, 20, 100);
    EXPECT_EQ(controller.target_kbps(), 1'000U);
}

} // namespace
