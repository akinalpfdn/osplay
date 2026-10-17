#include "crypto/primitives.hpp"
#include "crypto_test_support.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

namespace crypto = osplay::crypto;
using osplay::test::from_hex;
using osplay::test::key_from_hex;

std::vector<std::uint8_t> bytes(const std::string& text) {
    return {text.begin(), text.end()};
}

TEST(CryptoPrimitives, X25519MatchesRfc7748) {
    const auto alice =
        key_from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const auto bob =
        key_from_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const auto alice_public = crypto::x25519_public_key(alice);
    const auto bob_public = crypto::x25519_public_key(bob);
    EXPECT_EQ(alice_public,
              key_from_hex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    EXPECT_EQ(bob_public,
              key_from_hex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));

    const auto alice_shared = crypto::x25519(alice, bob_public);
    const auto bob_shared = crypto::x25519(bob, alice_public);
    ASSERT_TRUE(alice_shared.has_value());
    ASSERT_TRUE(bob_shared.has_value());
    EXPECT_EQ(alice_shared.value(), bob_shared.value());
}

TEST(CryptoPrimitives, X25519RejectsLowOrderPeerKeys) {
    const auto secret =
        key_from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const auto shared = crypto::x25519(secret, crypto::Key{}); // the zero point has order 4
    ASSERT_FALSE(shared.has_value());
    EXPECT_EQ(shared.error(), osplay::Error::InvalidKey);
}

TEST(CryptoPrimitives, Blake2bMatchesRfc7693) {
    const auto abc = bytes("abc");
    const auto hash = crypto::blake2b({abc});
    EXPECT_EQ(std::vector<std::uint8_t>(hash.begin(), hash.end()),
              from_hex("ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                       "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"));
}

// Expected values computed with Python's hmac and hashlib.blake2b, an independent implementation.
TEST(CryptoPrimitives, HmacBlake2bMatchesPythonStdlib) {
    std::vector<std::uint8_t> key(64);
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }
    const auto message = bytes("Osplay");
    const auto mac = crypto::hmac_blake2b(key, {message});
    EXPECT_EQ(std::vector<std::uint8_t>(mac.begin(), mac.end()),
              from_hex("889a14cd53ea4f5b1ae8d98ca167e74e59e5d377a147ed79cd03376041f2d8ee"
                       "d802e8e6eba2c34d2c81bebbe4da2d3e8963c1a9f0f0296a18217853fa608b38"));

    std::vector<std::uint8_t> long_key(200);
    for (std::size_t i = 0; i < long_key.size(); ++i) {
        long_key[i] = static_cast<std::uint8_t>(i);
    }
    const auto long_mac = crypto::hmac_blake2b(long_key, {message});
    EXPECT_EQ(std::vector<std::uint8_t>(long_mac.begin(), long_mac.end()),
              from_hex("4340115b618cee1aa659cbe7677dc494e21d9b4c6a848e85461ec9e89febeb1e"
                       "40c8866dcbd503b73f787eeb3efd4277f59919c8f8eeddd1aafda1bc7fb6f2b2"));
}

TEST(CryptoPrimitives, AeadRoundTripsAndDetectsTampering) {
    const auto key =
        key_from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const auto ad = bytes("header");
    const auto plaintext = bytes("frame payload");
    std::vector<std::uint8_t> sealed(plaintext.size() + crypto::kTagSize);
    crypto::aead_encrypt(key, 7, ad, plaintext, sealed);

    std::vector<std::uint8_t> opened(plaintext.size());
    EXPECT_TRUE(crypto::aead_decrypt(key, 7, ad, sealed, opened));
    EXPECT_EQ(opened, plaintext);

    EXPECT_FALSE(crypto::aead_decrypt(key, 8, ad, sealed, opened)) << "wrong nonce";
    auto other_ad = ad;
    other_ad[0] ^= 1U;
    EXPECT_FALSE(crypto::aead_decrypt(key, 7, other_ad, sealed, opened)) << "wrong associated data";
    auto tampered = sealed;
    tampered[3] ^= 1U;
    EXPECT_FALSE(crypto::aead_decrypt(key, 7, ad, tampered, opened)) << "tampered ciphertext";
    EXPECT_FALSE(crypto::aead_decrypt(key, 7, ad, std::span(sealed).first(10), opened))
        << "shorter than a tag";
}

} // namespace
