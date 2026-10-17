#include "fixtures.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/media_packets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <span>

namespace {

using osplay::test::Fixture;
namespace protocol = osplay::protocol;

// Every fixture reaches the outcome the manifest promises; valid ones re-encode byte for byte.
TEST(ProtocolFixtures, EveryFixtureMatchesTheManifest) {
    const auto fixtures = osplay::test::load_manifest();
    ASSERT_FALSE(fixtures.empty());
    for (const Fixture& fixture : fixtures) {
        SCOPED_TRACE(fixture.file + " — " + fixture.description);
        const auto bytes = osplay::test::read_fixture(fixture.file);

        if (fixture.kind == "control") {
            const auto decoded = protocol::decode_control(bytes);
            ASSERT_EQ(decoded.has_value(), fixture.valid);
            if (!fixture.valid) {
                EXPECT_EQ(static_cast<int>(decoded.error()), fixture.detail);
                continue;
            }
            EXPECT_EQ(static_cast<int>(protocol::type_of(decoded.value())), fixture.detail);
            const auto encoded = protocol::encode_control(decoded.value());
            ASSERT_TRUE(encoded.has_value());
            EXPECT_EQ(encoded.value(), bytes);
        } else {
            ASSERT_EQ(fixture.kind, "media");
            const auto decoded = protocol::decode_media(bytes);
            ASSERT_EQ(decoded.has_value(), fixture.valid);
            if (!fixture.valid) {
                EXPECT_EQ(static_cast<int>(decoded.error()), fixture.detail);
                continue;
            }
            EXPECT_EQ(static_cast<int>(protocol::type_of(decoded.value())), fixture.detail);
            std::array<std::uint8_t, protocol::kMaxMediaPlaintextSize> buffer{};
            const auto written = protocol::encode_media(decoded.value(), buffer);
            ASSERT_TRUE(written.has_value());
            EXPECT_TRUE(std::ranges::equal(std::span(buffer).first(written.value()), bytes));
        }
    }
}

TEST(ProtocolFixtures, CoverEveryMessageAndPacketType) {
    std::set<int> control_types;
    std::set<int> media_types;
    for (const Fixture& fixture : osplay::test::load_manifest()) {
        if (fixture.valid) {
            (fixture.kind == "control" ? control_types : media_types).insert(fixture.detail);
        }
    }
    for (int type = 0x01; type <= 0x13; ++type) {
        EXPECT_TRUE(control_types.contains(type)) << "no valid control fixture for type " << type;
    }
    for (int type = 0x01; type <= 0x04; ++type) {
        EXPECT_TRUE(media_types.contains(type)) << "no valid media fixture for type " << type;
    }
}

TEST(ProtocolFixtures, CoverEveryDecodingError) {
    std::set<int> errors;
    for (const Fixture& fixture : osplay::test::load_manifest()) {
        if (!fixture.valid) {
            errors.insert(fixture.detail);
        }
    }
    for (const auto error : {protocol::Error::Truncated, protocol::Error::BadMagic,
                             protocol::Error::UnsupportedVersion, protocol::Error::UnknownType,
                             protocol::Error::TrailingBytes, protocol::Error::ValueOutOfRange,
                             protocol::Error::InvalidString, protocol::Error::TooLarge}) {
        EXPECT_TRUE(errors.contains(static_cast<int>(error)))
            << "no fixture for error " << static_cast<int>(error);
    }
}

} // namespace
