#include <osplay/win/vdd_settings.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

using osplay::win::render_vdd_settings;
using osplay::win::helper::MonitorConfig;

std::size_t count_of(const std::string& haystack, const std::string& needle) {
    std::size_t count = 0;
    for (auto at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + 1)) {
        ++count;
    }
    return count;
}

TEST(VddSettings, DescribesExactlyOneMonitorWithOneMode) {
    const std::string xml = render_vdd_settings(
        {{.width = 2560, .height = 1664, .refresh_millihertz = 60'000, .gpu_name = ""}});

    EXPECT_NE(xml.find("<count>1</count>"), std::string::npos);
    EXPECT_EQ(count_of(xml, "<resolution>"), 1U);
    EXPECT_NE(xml.find("<width>2560</width>"), std::string::npos);
    EXPECT_NE(xml.find("<height>1664</height>"), std::string::npos);
    EXPECT_NE(xml.find("<refresh_rate>60</refresh_rate>"), std::string::npos);
    // Global refresh rates would multiply the mode list.
    EXPECT_EQ(xml.find("<g_refresh_rate>"), std::string::npos);
}

TEST(VddSettings, EmptyGpuNameLetsTheDriverChoose) {
    const std::string xml = render_vdd_settings(
        {{.width = 1920, .height = 1080, .refresh_millihertz = 60'000, .gpu_name = ""}});
    EXPECT_NE(xml.find("<friendlyname>default</friendlyname>"), std::string::npos);
}

TEST(VddSettings, EscapesGpuName) {
    const std::string xml = render_vdd_settings({{.width = 1920,
                                                  .height = 1080,
                                                  .refresh_millihertz = 60'000,
                                                  .gpu_name = "A&B <x> \"q\" 'y'"}});
    EXPECT_NE(
        xml.find("<friendlyname>A&amp;B &lt;x&gt; &quot;q&quot; &apos;y&apos;</friendlyname>"),
        std::string::npos);
}

TEST(VddSettings, FormatsFractionalRefreshRates) {
    const auto refresh = [](std::uint32_t millihertz) {
        return render_vdd_settings(
            {{.width = 1920, .height = 1080, .refresh_millihertz = millihertz, .gpu_name = ""}});
    };
    EXPECT_NE(refresh(59'940).find("<refresh_rate>59.94</refresh_rate>"), std::string::npos);
    EXPECT_NE(refresh(144'000).find("<refresh_rate>144</refresh_rate>"), std::string::npos);
    EXPECT_NE(refresh(23'976).find("<refresh_rate>23.976</refresh_rate>"), std::string::npos);
    EXPECT_NE(refresh(60'001).find("<refresh_rate>60.001</refresh_rate>"), std::string::npos);
}

TEST(VddSettings, GivesEachDeviceOneMonitorThatOffersEveryDistinctModeOnce) {
    const osplay::win::helper::MonitorConfig large{
        .width = 2560, .height = 1440, .refresh_millihertz = 60'000, .gpu_name = ""};
    const osplay::win::helper::MonitorConfig small{
        .width = 1920, .height = 1080, .refresh_millihertz = 60'000, .gpu_name = ""};
    const std::string xml = render_vdd_settings({large, small, large});
    // Every monitor has its own device; two on one device stall the desktop.
    EXPECT_NE(xml.find("<count>1</count>"), std::string::npos);
    EXPECT_EQ(count_of(xml, "<resolution>"), 2U);
    EXPECT_LT(xml.find("<width>2560</width>"), xml.find("<width>1920</width>"))
        << "the first monitor's mode is the preferred one";
}

TEST(VddSettings, KeepsHardwareCursorOutOfFrames) {
    const std::string xml = render_vdd_settings(
        {{.width = 1920, .height = 1080, .refresh_millihertz = 60'000, .gpu_name = ""}});
    EXPECT_NE(xml.find("<HardwareCursor>true</HardwareCursor>"), std::string::npos);
}

} // namespace
