#include "crypto/noise.hpp"
#include "crypto_test_support.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

namespace crypto = osplay::crypto;
using crypto::HandshakeRole;
using crypto::HandshakeState;
using osplay::Error;
using osplay::test::from_hex;

const std::vector<std::uint8_t> kPrologue{'o', 's', 'p', 'l', 'a', 'y'};

struct Pair {
    osplay::test::CountingRandom random;
    crypto::KeyPair initiator_static = osplay::test::key_pair(random);
    crypto::KeyPair responder_static = osplay::test::key_pair(random);
    HandshakeState initiator{HandshakeRole::Initiator, initiator_static, kPrologue, random};
    HandshakeState responder{HandshakeRole::Responder, responder_static, kPrologue, random};

    /// Runs all three messages; returns false at the first failure.
    bool run() {
        const auto m1 = initiator.write_message({});
        if (!m1 || !responder.read_message(m1.value())) {
            return false;
        }
        const auto m2 = responder.write_message({});
        if (!m2 || !initiator.read_message(m2.value())) {
            return false;
        }
        const auto m3 = initiator.write_message({});
        return m3 && responder.read_message(m3.value());
    }
};

TEST(NoiseHandshake, BothSidesAuthenticateAndAgreeOnKeys) {
    Pair pair;
    ASSERT_TRUE(pair.run());
    const auto initiator = pair.initiator.finish();
    const auto responder = pair.responder.finish();
    ASSERT_TRUE(initiator.has_value());
    ASSERT_TRUE(responder.has_value());

    EXPECT_EQ(initiator.value().remote_static, pair.responder_static.public_key());
    EXPECT_EQ(responder.value().remote_static, pair.initiator_static.public_key());
    EXPECT_EQ(initiator.value().handshake_hash, responder.value().handshake_hash);
    EXPECT_EQ(initiator.value().media_send, responder.value().media_receive);
    EXPECT_EQ(initiator.value().media_receive, responder.value().media_send);
    // Four distinct keys: per direction and per channel.
    EXPECT_NE(initiator.value().media_send, initiator.value().media_receive);

    auto send = initiator.value().control_send;
    auto receive = responder.value().control_receive;
    const std::vector<std::uint8_t> payload{1, 2, 3};
    const auto ciphertext = send.encrypt_with_ad({}, payload);
    ASSERT_TRUE(ciphertext.has_value());
    const auto plaintext = receive.decrypt_with_ad({}, ciphertext.value());
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_EQ(plaintext.value(), payload);
}

TEST(NoiseHandshake, PayloadsTravelWithTheHandshake) {
    Pair pair;
    const std::vector<std::uint8_t> hello{'h', 'i'};
    const auto m1 = pair.initiator.write_message(hello);
    ASSERT_TRUE(m1.has_value());
    const auto received = pair.responder.read_message(m1.value());
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received.value(), hello);
}

TEST(NoiseHandshake, MessagesOutOfOrderAreRefusedAndEndTheHandshake) {
    Pair pair;
    const auto early = pair.responder.write_message({});
    ASSERT_FALSE(early.has_value());
    EXPECT_EQ(early.error(), Error::HandshakeOutOfOrder);

    const auto m1 = pair.initiator.write_message({});
    ASSERT_TRUE(m1.has_value());
    const auto after_failure = pair.responder.read_message(m1.value());
    ASSERT_FALSE(after_failure.has_value());
    EXPECT_EQ(after_failure.error(), Error::HandshakeOutOfOrder);

    const auto too_early_finish = pair.initiator.finish();
    ASSERT_FALSE(too_early_finish.has_value());
    EXPECT_EQ(too_early_finish.error(), Error::HandshakeOutOfOrder);
}

TEST(NoiseHandshake, TamperedMessagesAreRejected) {
    Pair pair;
    const auto m1 = pair.initiator.write_message({});
    ASSERT_TRUE(m1.has_value());
    ASSERT_TRUE(pair.responder.read_message(m1.value()).has_value());
    auto m2 = pair.responder.write_message({});
    ASSERT_TRUE(m2.has_value());
    m2.value()[40] ^= 1U; // inside the encrypted static key
    const auto result = pair.initiator.read_message(m2.value());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::DecryptionFailed);
}

TEST(NoiseHandshake, DifferentProloguesFailTheHandshake) {
    osplay::test::CountingRandom random;
    const auto initiator_static = osplay::test::key_pair(random);
    const auto responder_static = osplay::test::key_pair(random);
    const std::vector<std::uint8_t> other{'x'};
    HandshakeState initiator(HandshakeRole::Initiator, initiator_static, kPrologue, random);
    HandshakeState responder(HandshakeRole::Responder, responder_static, other, random);

    const auto m1 = initiator.write_message({});
    ASSERT_TRUE(m1.has_value());
    ASSERT_TRUE(responder.read_message(m1.value()).has_value());
    const auto m2 = responder.write_message({});
    ASSERT_TRUE(m2.has_value());
    const auto result = initiator.read_message(m2.value());
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::DecryptionFailed);
}

TEST(NoiseHandshake, LowOrderEphemeralKeysAreRejected) {
    Pair pair;
    const std::vector<std::uint8_t> zero_ephemeral(crypto::kKeySize, 0);
    ASSERT_TRUE(pair.responder.read_message(zero_ephemeral).has_value());
    const auto m2 = pair.responder.write_message({});
    ASSERT_FALSE(m2.has_value());
    EXPECT_EQ(m2.error(), Error::InvalidKey);
}

TEST(NoiseHandshake, TruncatedAndOversizedMessagesAreRejected) {
    Pair pair;
    const auto short_message = pair.responder.read_message(std::vector<std::uint8_t>(31));
    ASSERT_FALSE(short_message.has_value());
    EXPECT_EQ(short_message.error(), Error::Truncated);

    Pair other;
    const auto huge =
        other.responder.read_message(std::vector<std::uint8_t>(crypto::kMaxNoiseMessageSize + 1));
    ASSERT_FALSE(huge.has_value());
    EXPECT_EQ(huge.error(), Error::TooLarge);
}

TEST(NoiseTransport, TamperedReplayedAndReorderedMessagesAreRejected) {
    Pair pair;
    ASSERT_TRUE(pair.run());
    auto send = pair.initiator.finish().value().control_send;
    auto receive = pair.responder.finish().value().control_receive;
    const std::vector<std::uint8_t> first{1};
    const std::vector<std::uint8_t> second{2};
    const auto c1 = send.encrypt_with_ad({}, first);
    const auto c2 = send.encrypt_with_ad({}, second);
    ASSERT_TRUE(c1.has_value());
    ASSERT_TRUE(c2.has_value());

    auto tampered = c1.value();
    tampered[0] ^= 1U;
    EXPECT_EQ(receive.decrypt_with_ad({}, tampered).error(), Error::DecryptionFailed);
    EXPECT_EQ(receive.nonce(), 0U) << "a forged message must not advance the nonce";

    EXPECT_EQ(receive.decrypt_with_ad({}, c2.value()).error(), Error::DecryptionFailed)
        << "reordered";
    ASSERT_TRUE(receive.decrypt_with_ad({}, c1.value()).has_value());
    EXPECT_EQ(receive.decrypt_with_ad({}, c1.value()).error(), Error::DecryptionFailed)
        << "replayed";
    EXPECT_TRUE(receive.decrypt_with_ad({}, c2.value()).has_value());
}

TEST(PairingCode, BothSidesDeriveTheSameSixDigitCode) {
    Pair pair;
    ASSERT_TRUE(pair.run());
    const auto initiator_code = crypto::pairing_code(pair.initiator.handshake_hash());
    EXPECT_EQ(initiator_code, crypto::pairing_code(pair.responder.handshake_hash()));
    EXPECT_LT(initiator_code, 1'000'000U);
}

TEST(PairingCode, AnyChangeToTheTranscriptChangesTheCode) {
    Pair reference;
    ASSERT_TRUE(reference.run());
    const auto reference_code = crypto::pairing_code(reference.initiator.handshake_hash());

    // Same keys and randomness, one payload byte different.
    Pair changed;
    const auto m1 = changed.initiator.write_message(std::vector<std::uint8_t>{1});
    ASSERT_TRUE(m1.has_value());
    ASSERT_TRUE(changed.responder.read_message(m1.value()).has_value());
    const auto m2 = changed.responder.write_message({});
    ASSERT_TRUE(m2.has_value());
    ASSERT_TRUE(changed.initiator.read_message(m2.value()).has_value());
    const auto m3 = changed.initiator.write_message({});
    ASSERT_TRUE(m3.has_value());
    ASSERT_TRUE(changed.responder.read_message(m3.value()).has_value());
    EXPECT_NE(crypto::pairing_code(changed.initiator.handshake_hash()), reference_code);

    crypto::Hash flipped = reference.initiator.handshake_hash();
    for (std::size_t bit = 0; bit < flipped.size() * 8; bit += 61) {
        crypto::Hash variant = flipped;
        variant.at(bit / 8) ^= static_cast<std::uint8_t>(1U << (bit % 8));
        EXPECT_NE(crypto::pairing_code(variant), reference_code) << "bit " << bit;
    }
}

// Expected values computed with Python's hashlib.blake2b (docs/PROTOCOL.md defines the
// derivations).
TEST(PairingCode, MatchesTheDocumentedDerivation) {
    const auto hash = from_hex("8cf47d7b3cb5804c0109d48e8bcdbee2cbb65687d8ea2c92994ca361fb86151a"
                               "d93627b98936cbb32de56e8abb21def3925011ac3e35db9cbeea73ab9a4392c2");
    crypto::Hash handshake_hash{};
    std::copy(hash.begin(), hash.end(), handshake_hash.begin());
    EXPECT_EQ(crypto::pairing_code(handshake_hash), 79'588U);
}

TEST(DeviceId, MatchesTheDocumentedDerivation) {
    const auto id = crypto::device_id(osplay::test::key_from_hex(
        "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    EXPECT_EQ(std::vector<std::uint8_t>(id.begin(), id.end()),
              from_hex("7a91869e4c114964206972403b0a3d81"));
}

TEST(KeyPair, RejectsAnAllZeroSecretAndReportsRandomFailure) {
    EXPECT_EQ(crypto::KeyPair::from_secret(crypto::Key{}).error(), Error::InvalidKey);
    osplay::test::ScriptedRandom empty;
    EXPECT_EQ(crypto::KeyPair::generate(empty).error(), Error::RandomUnavailable);
}

} // namespace
