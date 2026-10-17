#include "crypto_test_support.hpp"

#include <osplay/osplay.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

using osplay::test::from_hex;

const std::vector<std::uint8_t> kAliceSecret =
    from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
const std::vector<std::uint8_t> kAlicePublic =
    from_hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");

osplay_result alice_random(void* /*context*/, uint8_t* buffer, size_t length) {
    if (length != kAliceSecret.size()) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    std::copy(kAliceSecret.begin(), kAliceSecret.end(), buffer);
    return OSPLAY_OK;
}

osplay_result broken_random(void* /*context*/, uint8_t* /*buffer*/, size_t /*length*/) {
    return OSPLAY_ERROR_RANDOM_UNAVAILABLE;
}

TEST(CapiIdentity, GeneratesFromThePlatformRandomSource) {
    const osplay_random_source random{.fill = alice_random, .context = nullptr};
    std::array<std::uint8_t, OSPLAY_KEY_SIZE> secret{};
    std::array<std::uint8_t, OSPLAY_KEY_SIZE> public_key{};
    ASSERT_EQ(osplay_identity_generate(&random, secret.data(), public_key.data()), OSPLAY_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(secret.begin(), secret.end()), kAliceSecret);
    EXPECT_EQ(std::vector<std::uint8_t>(public_key.begin(), public_key.end()), kAlicePublic);
}

TEST(CapiIdentity, ReportsRandomFailureAndInvalidArguments) {
    const osplay_random_source broken{.fill = broken_random, .context = nullptr};
    std::array<std::uint8_t, OSPLAY_KEY_SIZE> secret{};
    std::array<std::uint8_t, OSPLAY_KEY_SIZE> public_key{};
    EXPECT_EQ(osplay_identity_generate(&broken, secret.data(), public_key.data()),
              OSPLAY_ERROR_RANDOM_UNAVAILABLE);
    const osplay_random_source missing{.fill = nullptr, .context = nullptr};
    EXPECT_EQ(osplay_identity_generate(&missing, secret.data(), public_key.data()),
              OSPLAY_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(osplay_identity_generate(nullptr, secret.data(), public_key.data()),
              OSPLAY_ERROR_INVALID_ARGUMENT);
}

TEST(CapiIdentity, ImportsAStoredSecretAndRejectsAZeroOne) {
    std::array<std::uint8_t, OSPLAY_KEY_SIZE> public_key{};
    ASSERT_EQ(osplay_identity_public_key(kAliceSecret.data(), public_key.data()), OSPLAY_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(public_key.begin(), public_key.end()), kAlicePublic);

    const std::array<std::uint8_t, OSPLAY_KEY_SIZE> zero{};
    EXPECT_EQ(osplay_identity_public_key(zero.data(), public_key.data()), OSPLAY_ERROR_INVALID_KEY);
}

TEST(CapiIdentity, DerivesTheDeviceId) {
    std::array<std::uint8_t, OSPLAY_DEVICE_ID_SIZE> id{};
    ASSERT_EQ(osplay_identity_device_id(kAlicePublic.data(), id.data()), OSPLAY_OK);
    EXPECT_EQ(std::vector<std::uint8_t>(id.begin(), id.end()),
              from_hex("7a91869e4c114964206972403b0a3d81"));
    EXPECT_EQ(osplay_identity_device_id(nullptr, id.data()), OSPLAY_ERROR_INVALID_ARGUMENT);
}

} // namespace
