#include "fixtures.hpp"

#include <osplay/osplay.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

osplay_wire_kind kind_of(const osplay::test::Fixture& fixture) {
    return fixture.kind == "control" ? OSPLAY_WIRE_CONTROL_MESSAGE : OSPLAY_WIRE_MEDIA_PACKET;
}

TEST(CapiProtocol, ValidateAndReencodeAgreeWithTheManifest) {
    for (const auto& fixture : osplay::test::load_manifest()) {
        SCOPED_TRACE(fixture.file);
        const auto bytes = osplay::test::read_fixture(fixture.file);
        std::uint8_t type = 0;
        const osplay_result validated =
            osplay_protocol_validate(kind_of(fixture), bytes.data(), bytes.size(), &type);
        if (!fixture.valid) {
            EXPECT_EQ(static_cast<int>(validated), fixture.detail);
            continue;
        }
        ASSERT_EQ(validated, OSPLAY_OK);
        EXPECT_EQ(type, fixture.detail);

        std::array<std::uint8_t, OSPLAY_MAX_CONTROL_MESSAGE_SIZE> out{};
        std::size_t length = 0;
        ASSERT_EQ(osplay_protocol_reencode(kind_of(fixture), bytes.data(), bytes.size(), out.data(),
                                           out.size(), &length),
                  OSPLAY_OK);
        EXPECT_EQ(std::vector<std::uint8_t>(out.begin(),
                                            out.begin() + static_cast<std::ptrdiff_t>(length)),
                  bytes);
    }
}

TEST(CapiProtocol, RejectsInvalidArguments) {
    std::uint8_t type = 0;
    std::size_t length = 0;
    const std::array<std::uint8_t, 4> data{'O', 'P', 1, 0x10};
    EXPECT_EQ(osplay_protocol_validate(OSPLAY_WIRE_CONTROL_MESSAGE, nullptr, 4, &type),
              OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(
        osplay_protocol_validate(OSPLAY_WIRE_CONTROL_MESSAGE, data.data(), data.size(), nullptr),
        OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(
        osplay_protocol_validate(static_cast<osplay_wire_kind>(9), data.data(), data.size(), &type),
        OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osplay_protocol_validate(OSPLAY_WIRE_CONTROL_MESSAGE, nullptr, 0, &type),
              OSPLAY_ERROR_TRUNCATED);
    EXPECT_EQ(osplay_protocol_reencode(OSPLAY_WIRE_CONTROL_MESSAGE, data.data(), data.size(),
                                       nullptr, 0, &length),
              OSPLAY_ERROR_BUFFER_TOO_SMALL);
    EXPECT_EQ(osplay_protocol_reencode(OSPLAY_WIRE_CONTROL_MESSAGE, data.data(), data.size(),
                                       nullptr, 0, nullptr),
              OSPLAY_ERROR_INVALID_ARGUMENT);
}

} // namespace
