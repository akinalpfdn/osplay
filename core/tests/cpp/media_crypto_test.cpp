#include "crypto/media_crypto.hpp"
#include "crypto_test_support.hpp"
#include "protocol/media_packets.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

namespace crypto = osplay::crypto;
namespace protocol = osplay::protocol;
using osplay::Error;

const crypto::Key kKey =
    osplay::test::key_from_hex("101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f");

/// An encoded VideoFragment with `sequence`, in a buffer with room for the tag.
std::vector<std::uint8_t> datagram(std::uint64_t sequence, std::size_t* packet_size) {
    static const std::array<std::uint8_t, 100> payload{1, 2, 3};
    const protocol::MediaPacket packet{
        .sequence = sequence,
        .body =
            protocol::VideoFragment{.fragment_index = 0, .fragment_count = 1, .payload = payload}};
    std::vector<std::uint8_t> buffer(protocol::kMaxDatagramSize);
    const auto written = protocol::encode_media(packet, buffer);
    *packet_size = written.value();
    return buffer;
}

std::vector<std::uint8_t> sealed(crypto::MediaSealer& sealer, std::uint64_t sequence) {
    std::size_t packet_size = 0;
    auto buffer = datagram(sequence, &packet_size);
    const auto size = sealer.seal(buffer, packet_size);
    if (!size) {
        ADD_FAILURE() << "seal failed";
        return {};
    }
    buffer.resize(size.value());
    return buffer;
}

TEST(ReplayWindow, AcceptsEachSequenceOnceInAnyOrderWithinTheWindow) {
    crypto::ReplayWindow window;
    EXPECT_TRUE(window.is_new(5));
    window.accept(5);
    EXPECT_FALSE(window.is_new(5));
    EXPECT_TRUE(window.is_new(3)); // late but within the window
    window.accept(3);
    EXPECT_FALSE(window.is_new(3));
    window.accept(5 + crypto::ReplayWindow::kSize);
    EXPECT_FALSE(window.is_new(5)) << "older than the window";
    EXPECT_TRUE(window.is_new(6)) << "the oldest number still inside the window";
    window.accept(100'000);
    EXPECT_FALSE(window.is_new(5 + crypto::ReplayWindow::kSize))
        << "jump clears and moves the window";
}

TEST(MediaCrypto, SealedDatagramsOpenAndDecodeAgain) {
    crypto::MediaSealer sealer(kKey);
    crypto::MediaOpener opener(kKey);
    auto wire = sealed(sealer, 1);
    ASSERT_LE(wire.size(), protocol::kMaxDatagramSize);
    std::size_t original_size = 0;
    const auto original = datagram(1, &original_size);
    EXPECT_TRUE(std::equal(wire.begin(), wire.begin() + 12, original.begin()))
        << "the header stays in clear";
    EXPECT_FALSE(std::equal(wire.begin() + 12, wire.begin() + 30, original.begin() + 12))
        << "the body is encrypted";

    const auto opened = opener.open(wire);
    ASSERT_TRUE(opened.has_value());
    const auto decoded = protocol::decode_media(opened.value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().sequence, 1U);
}

TEST(MediaCrypto, TamperedHeaderOrBodyIsRejected) {
    crypto::MediaSealer sealer(kKey);
    crypto::MediaOpener opener(kKey);
    auto header_changed = sealed(sealer, 1);
    header_changed[3] ^= 1U; // packet type: authenticated as associated data
    EXPECT_EQ(opener.open(header_changed).error(), Error::DecryptionFailed);

    auto body_changed = sealed(sealer, 2);
    body_changed[20] ^= 1U;
    EXPECT_EQ(opener.open(body_changed).error(), Error::DecryptionFailed);

    auto good = sealed(sealer, 3);
    EXPECT_TRUE(opener.open(good).has_value()) << "forgeries must not disturb later packets";
}

TEST(MediaCrypto, ReplayedAndStaleDatagramsAreRejectedReorderedOnesAccepted) {
    crypto::MediaSealer sealer(kKey);
    crypto::MediaOpener opener(kKey);
    auto first = sealed(sealer, 10);
    auto second = sealed(sealer, 11);
    auto replay = second;

    ASSERT_TRUE(opener.open(second).has_value());
    EXPECT_TRUE(opener.open(first).has_value()) << "reordered within the window";
    EXPECT_EQ(opener.open(replay).error(), Error::Replayed);

    auto far_ahead = sealed(sealer, 10 + 2 * crypto::ReplayWindow::kSize);
    ASSERT_TRUE(opener.open(far_ahead).has_value());
    crypto::MediaSealer old_sender(kKey);
    auto stale = sealed(old_sender, 12);
    EXPECT_EQ(opener.open(stale).error(), Error::Replayed);
}

TEST(MediaCrypto, SealerNeverReusesASequenceNumber) {
    crypto::MediaSealer sealer(kKey);
    std::size_t packet_size = 0;
    auto first = datagram(5, &packet_size);
    ASSERT_TRUE(sealer.seal(first, packet_size).has_value());
    auto again = datagram(5, &packet_size);
    EXPECT_EQ(sealer.seal(again, packet_size).error(), Error::InvalidArgument);
    auto older = datagram(4, &packet_size);
    EXPECT_EQ(sealer.seal(older, packet_size).error(), Error::InvalidArgument);
    std::vector<std::uint8_t> no_room(packet_size);
    auto newer = datagram(6, &packet_size);
    std::copy_n(newer.begin(), packet_size, no_room.begin());
    EXPECT_EQ(sealer.seal(no_room, packet_size).error(), Error::BufferTooSmall);
}

TEST(MediaCrypto, OpenerRejectsMalformedSizes) {
    crypto::MediaOpener opener(kKey);
    std::vector<std::uint8_t> tiny(27);
    EXPECT_EQ(opener.open(tiny).error(), Error::Truncated);
    std::vector<std::uint8_t> huge(protocol::kMaxDatagramSize + 1);
    EXPECT_EQ(opener.open(huge).error(), Error::TooLarge);
}

} // namespace
