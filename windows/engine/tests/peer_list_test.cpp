#include "keystore/peer_list.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using osplay::engine::PeerList;
using osplay::engine::PublicKey;

PublicKey key(std::uint8_t seed) {
    PublicKey value{};
    value.fill(seed);
    return value;
}

TEST(PeerList, AnUnknownKeyIsAskedToPairAndATrustedOneIsNot) {
    PeerList list;
    EXPECT_EQ(list.trust(key(1)), OSPLAY_TRUST_UNKNOWN);
    list.trust(key(1), "MacBook Air", 100);
    EXPECT_EQ(list.trust(key(1)), OSPLAY_TRUST_TRUSTED);
    EXPECT_EQ(list.trust(key(2)), OSPLAY_TRUST_UNKNOWN);
}

TEST(PeerList, ARevokedPeerIsRefusedUntilPairedAgainDeliberately) {
    PeerList list;
    list.trust(key(1), "MacBook Air", 100);
    EXPECT_TRUE(list.revoke(key(1)));
    EXPECT_EQ(list.trust(key(1)), OSPLAY_TRUST_BLOCKED);
    EXPECT_FALSE(list.revoke(key(9)));
    list.trust(key(1), "MacBook Air", 200);
    EXPECT_EQ(list.trust(key(1)), OSPLAY_TRUST_TRUSTED);
}

TEST(PeerList, TheFileFormatRoundTrips) {
    PeerList list;
    list.trust(key(1), "MacBook Air", 1'760'000'000);
    list.trust(key(2), "Çalışma Masası", -5);
    list.revoke(key(2));

    const auto decoded = PeerList::decode(list.encode());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->peers().size(), 2U);
    EXPECT_EQ(decoded->peers()[0].name, "MacBook Air");
    EXPECT_EQ(decoded->peers()[0].last_seen_unix, 1'760'000'000);
    EXPECT_EQ(decoded->peers()[1].name, "Çalışma Masası");
    EXPECT_TRUE(decoded->peers()[1].revoked);
    EXPECT_EQ(decoded->trust(key(2)), OSPLAY_TRUST_BLOCKED);
}

TEST(PeerList, DamagedFilesAreRejected) {
    PeerList list;
    list.trust(key(1), "MacBook Air", 1);
    std::vector<std::uint8_t> bytes = list.encode();
    bytes.pop_back();
    EXPECT_FALSE(PeerList::decode(bytes).has_value());
    bytes = list.encode();
    bytes.push_back(0);
    EXPECT_FALSE(PeerList::decode(bytes).has_value());
    bytes = list.encode();
    bytes[4] = 9; // unknown version
    EXPECT_FALSE(PeerList::decode(bytes).has_value());
}

TEST(PeerList, LongNamesAreCutToTheProtocolLimit) {
    PeerList list;
    list.trust(key(1), std::string(100, 'x'), 1);
    EXPECT_EQ(list.peers()[0].name.size(), 64U);
    EXPECT_TRUE(list.remove(key(1)));
    EXPECT_TRUE(list.peers().empty());
}

} // namespace
