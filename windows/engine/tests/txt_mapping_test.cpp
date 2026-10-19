#include "discovery/txt_mapping.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

namespace {

/// Copies a literal and its NUL into a fixed C string field.
template <std::size_t Size>
void set_text(char (&field)[Size], const char* text) { // NOLINT(*-avoid-c-arrays): C API field
    static_cast<void>(std::snprintf(static_cast<char*>(field), Size, "%s", text));
}

using osplay::engine::instance_label;
using osplay::engine::label_of;
using osplay::engine::txt_pairs;
using osplay::engine::txt_record;

osplay_txt_record mac_record() {
    osplay_advertisement advertisement{};
    advertisement.protocol_version = OSPLAY_PROTOCOL_VERSION;
    advertisement.platform = OSPLAY_PLATFORM_MACOS;
    advertisement.roles = OSPLAY_ROLE_HOST;
    advertisement.device_id[0] = 0xAB;
    set_text(advertisement.name, "MacBook Air");
    osplay_txt_record record{};
    osplay_advertisement_to_txt(&advertisement, &record);
    return record;
}

TEST(TxtMapping, PairsCarryTheFiveKeysAndReadBack) {
    const osplay_txt_record record = mac_record();
    const auto pairs = txt_pairs(record);
    ASSERT_EQ(pairs.size(), 5U);
    EXPECT_EQ(pairs[1].first, "name");
    EXPECT_EQ(pairs[1].second, "MacBook Air");

    auto shuffled = pairs;
    std::swap(shuffled[0], shuffled[4]);
    shuffled.emplace_back("future", "ignored");
    osplay_advertisement parsed{};
    const osplay_txt_record back = txt_record(shuffled);
    EXPECT_EQ(osplay_advertisement_from_txt(&back, &parsed), OSPLAY_OK);
    EXPECT_STREQ(parsed.name, "MacBook Air");
}

TEST(TxtMapping, AMissingKeyIsRejectedByTheCore) {
    auto pairs = txt_pairs(mac_record());
    pairs.erase(pairs.begin() + 3); // roles
    const osplay_txt_record record = txt_record(pairs);
    osplay_advertisement parsed{};
    EXPECT_NE(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_OK);
}

TEST(TxtMapping, InstanceNamesUseTheDeviceId) {
    const std::string label = instance_label(mac_record());
    EXPECT_EQ(label, "ab000000000000000000000000000000");
    EXPECT_EQ(label_of(label + "._osplay._tcp.local"), label);
    EXPECT_EQ(label_of(label + "._osplay._tcp.local."), label);
    EXPECT_FALSE(label_of("printer._ipp._tcp.local").has_value());
    EXPECT_FALSE(label_of("._osplay._tcp.local").has_value());
}

} // namespace
