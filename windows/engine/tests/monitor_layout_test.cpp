#include "viewer/monitor_layout.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using osplay::engine::mode_changed;
using osplay::engine::MonitorCandidate;
using osplay::engine::MonitorRect;
using osplay::engine::offer_monitors;
using osplay::engine::protocol_name;
using osplay::engine::still_attached;

MonitorCandidate candidate(const std::string& key, std::int32_t left, std::int32_t top = 0) {
    return {.key = key,
            .rect =
                MonitorRect{.left = left, .top = top, .right = left + 2560, .bottom = top + 1440},
            .width_px = 2560,
            .height_px = 1440,
            .refresh_millihertz = 59'951,
            .physical_width_mm = 597,
            .physical_height_mm = 336,
            .dpi = 144,
            .name_utf8 = "DELL U2720Q"};
}

TEST(MonitorLayout, MonitorsAreNumberedLeftToRight) {
    const auto offered =
        offer_monitors({candidate("B", 2560), candidate("A", -2560), candidate("C", 0)});
    ASSERT_EQ(offered.size(), 3U);
    EXPECT_EQ(offered[0].key, "A");
    EXPECT_EQ(offered[1].key, "C");
    EXPECT_EQ(offered[2].key, "B");
    EXPECT_EQ(offered[2].monitor.id, 2);
    EXPECT_EQ(offered[2].monitor.description.monitor_id, 2);
}

TEST(MonitorLayout, ValuesAreWhatTheProtocolAccepts) {
    MonitorCandidate unknown = candidate("A", 0);
    unknown.refresh_millihertz = 0;
    unknown.dpi = 72;
    unknown.physical_width_mm = 20'000;
    const auto offered = offer_monitors({candidate("B", 2560), unknown});
    EXPECT_EQ(offered[0].monitor.description.refresh_millihertz, 60'000U);
    EXPECT_EQ(offered[0].monitor.description.scale_percent, 100);
    EXPECT_EQ(offered[0].monitor.description.physical_width_mm, 10'000);
    EXPECT_EQ(offered[1].monitor.description.refresh_millihertz, 59'951U);
    EXPECT_EQ(offered[1].monitor.description.scale_percent, 150);
    EXPECT_EQ(offered[1].name, "DELL U2720Q");
}

TEST(MonitorLayout, TooSmallMonitorsAreLeftOutAndAtMostFourOffered) {
    MonitorCandidate tiny = candidate("tiny", -100);
    tiny.width_px = 200;
    const auto offered =
        offer_monitors({tiny, candidate("A", 0), candidate("B", 2560), candidate("C", 5120),
                        candidate("D", 7680), candidate("E", 10'240)});
    ASSERT_EQ(offered.size(), 4U);
    EXPECT_EQ(offered[0].key, "A");
    EXPECT_EQ(offered[3].key, "D");
}

TEST(MonitorLayout, NamesLoseControlCharactersAndFitTheProtocol) {
    EXPECT_EQ(protocol_name("A\tB\x7F"
                            "C"),
              "ABC");
    const std::string long_name(70, 'x');
    EXPECT_EQ(protocol_name(long_name).size(), 64U);
    // 21 three-byte characters are 63 bytes; the 22nd would cross the limit.
    std::string wide;
    for (int i = 0; i < 22; ++i) {
        wide += "\xE2\x82\xAC"; // €
    }
    EXPECT_EQ(protocol_name(wide).size(), 63U);
    EXPECT_EQ(protocol_name("ok\xFFgone"), "ok");
}

TEST(MonitorLayout, AnUnpluggedMonitorIsNoLongerAttached) {
    const auto offered = offer_monitors({candidate("A", 0), candidate("B", 2560)});
    // After the change Windows renumbers; the key still identifies the remaining monitor.
    EXPECT_EQ(still_attached(offered, {candidate("B", 0)}), (std::set<std::uint8_t>{1}));
    EXPECT_EQ(still_attached(offered, {}), (std::set<std::uint8_t>{}));
}

TEST(MonitorLayout, ANewResolutionOrScalingIsAModeChange) {
    const auto offered = offer_monitors({candidate("A", 0), candidate("B", 2560)});
    EXPECT_FALSE(mode_changed(offered, {candidate("A", 0), candidate("B", 2560)}));
    EXPECT_FALSE(mode_changed(offered, {candidate("B", 0)})) << "an unplugged monitor is not one";

    MonitorCandidate smaller = candidate("B", 2560);
    smaller.width_px = 1920;
    smaller.height_px = 1080;
    EXPECT_TRUE(mode_changed(offered, {candidate("A", 0), smaller}));

    MonitorCandidate scaled = candidate("A", 0);
    scaled.dpi = 96;
    EXPECT_TRUE(mode_changed(offered, {scaled, candidate("B", 2560)}));
}

} // namespace
