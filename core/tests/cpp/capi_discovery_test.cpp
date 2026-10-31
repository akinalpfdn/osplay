#include <osplay/osplay.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace {

/// Copies a literal and its NUL into a fixed C string field.
template <std::size_t Size>
void set_text(char (&field)[Size], const char* text) { // NOLINT(*-avoid-c-arrays): C API field
    static_cast<void>(std::snprintf(static_cast<char*>(field), Size, "%s", text));
}

osplay_advertisement windows_host() {
    osplay_advertisement advertisement{};
    advertisement.protocol_version = OSPLAY_PROTOCOL_VERSION;
    advertisement.platform = OSPLAY_PLATFORM_WINDOWS;
    advertisement.roles = OSPLAY_ROLE_HOST | OSPLAY_ROLE_VIEWER;
    advertisement.protocol_minor = OSPLAY_PROTOCOL_MINOR;
    for (std::size_t i = 0; i < OSPLAY_DEVICE_ID_SIZE; ++i) {
        advertisement.device_id[i] = static_cast<std::uint8_t>(0xA0 + i);
    }
    set_text(advertisement.name, "Akinalp's PC");
    return advertisement;
}

TEST(Discovery, TheTxtRecordUsesTheDocumentedValues) {
    const osplay_advertisement advertisement = windows_host();
    osplay_txt_record record{};
    ASSERT_EQ(osplay_advertisement_to_txt(&advertisement, &record), OSPLAY_OK);
    EXPECT_STREQ(record.protocol_version, "1");
    EXPECT_STREQ(record.name, "Akinalp's PC");
    EXPECT_STREQ(record.device_id, "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    EXPECT_STREQ(record.roles, "host,viewer");
    EXPECT_STREQ(record.os, "windows");
    EXPECT_STREQ(record.protocol_minor, "2");
}

TEST(Discovery, ARecordReadsBackAsTheSameAdvertisement) {
    const osplay_advertisement original = windows_host();
    osplay_txt_record record{};
    ASSERT_EQ(osplay_advertisement_to_txt(&original, &record), OSPLAY_OK);
    osplay_advertisement parsed{};
    ASSERT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_OK);
    EXPECT_EQ(parsed.protocol_version, original.protocol_version);
    EXPECT_EQ(parsed.platform, original.platform);
    EXPECT_EQ(parsed.roles, original.roles);
    EXPECT_EQ(parsed.protocol_minor, original.protocol_minor);
    EXPECT_EQ(std::memcmp(parsed.device_id, original.device_id, OSPLAY_DEVICE_ID_SIZE), 0);
    EXPECT_STREQ(parsed.name, original.name);
}

TEST(Discovery, TabletsAdvertiseTheirPlatform) {
    osplay_advertisement tablet = windows_host();
    tablet.roles = OSPLAY_ROLE_VIEWER;
    for (const auto& [platform, text] : {std::pair{OSPLAY_PLATFORM_IPADOS, "ipados"},
                                         std::pair{OSPLAY_PLATFORM_ANDROID, "android"}}) {
        tablet.platform = platform;
        osplay_txt_record record{};
        ASSERT_EQ(osplay_advertisement_to_txt(&tablet, &record), OSPLAY_OK);
        EXPECT_STREQ(record.os, text);
        osplay_advertisement parsed{};
        ASSERT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_OK);
        EXPECT_EQ(parsed.platform, platform);
    }
}

TEST(Discovery, ARecordWithoutTheMinorVersionIsOsplay01) {
    const osplay_advertisement advertisement = windows_host();
    osplay_txt_record record{};
    ASSERT_EQ(osplay_advertisement_to_txt(&advertisement, &record), OSPLAY_OK);
    record.protocol_minor[0] = '\0'; // what Osplay 0.1 sends
    osplay_advertisement parsed{};
    ASSERT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_OK);
    EXPECT_EQ(parsed.protocol_minor, 0);
}

TEST(Discovery, ANewerProtocolVersionIsReportedNotRefused) {
    const osplay_advertisement advertisement = windows_host();
    osplay_txt_record record{};
    ASSERT_EQ(osplay_advertisement_to_txt(&advertisement, &record), OSPLAY_OK);
    set_text(record.protocol_version, "2");
    osplay_advertisement parsed{};
    ASSERT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_OK);
    EXPECT_EQ(parsed.protocol_version, 2U);
}

TEST(Discovery, MalformedRecordsAreRejected) {
    const osplay_advertisement advertisement = windows_host();
    osplay_txt_record good{};
    ASSERT_EQ(osplay_advertisement_to_txt(&advertisement, &good), OSPLAY_OK);
    osplay_advertisement parsed{};

    osplay_txt_record record = good;
    set_text(record.device_id, "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF"); // uppercase is not the format
    EXPECT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
    record = good;
    set_text(record.roles, "viewer,host");
    EXPECT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
    record = good;
    set_text(record.os, "linux");
    EXPECT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
    record = good;
    record.name[0] = '\0';
    EXPECT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_ERROR_INVALID_STRING);
    record = good;
    set_text(record.protocol_version, "1x");
    EXPECT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
    for (const char* minor : {"0", "256", "2x", "-1"}) {
        record = good;
        set_text(record.protocol_minor, minor);
        EXPECT_EQ(osplay_advertisement_from_txt(&record, &parsed), OSPLAY_ERROR_VALUE_OUT_OF_RANGE)
            << minor;
    }
}

TEST(Discovery, InvalidAdvertisementsAreNotPublished) {
    osplay_txt_record record{};
    osplay_advertisement advertisement = windows_host();
    advertisement.roles = 0;
    EXPECT_EQ(osplay_advertisement_to_txt(&advertisement, &record),
              OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
    advertisement = windows_host();
    std::memset(advertisement.name, 'x', sizeof(advertisement.name)); // no NUL: 65 bytes
    EXPECT_EQ(osplay_advertisement_to_txt(&advertisement, &record), OSPLAY_ERROR_INVALID_STRING);
    advertisement = windows_host();
    set_text(advertisement.name, "tab\there");
    EXPECT_EQ(osplay_advertisement_to_txt(&advertisement, &record), OSPLAY_ERROR_INVALID_STRING);
    advertisement = windows_host();
    advertisement.protocol_minor = 0; // a device must say which minor version it speaks
    EXPECT_EQ(osplay_advertisement_to_txt(&advertisement, &record),
              OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
    advertisement = windows_host();
    advertisement.platform = static_cast<osplay_platform>(5);
    EXPECT_EQ(osplay_advertisement_to_txt(&advertisement, &record),
              OSPLAY_ERROR_VALUE_OUT_OF_RANGE);
}

} // namespace
