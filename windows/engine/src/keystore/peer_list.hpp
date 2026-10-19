#pragma once

// Trusted and revoked peers (DEVPLAN: public key, display name, last seen) and their file format.
// The Windows key store encrypts the bytes with DPAPI; this part is portable and unit tested.

#include <osplay/osplay.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace osplay::engine {

using PublicKey = std::array<std::uint8_t, OSPLAY_KEY_SIZE>;

struct Peer {
    PublicKey public_key{};
    std::string name; ///< UTF-8, at most 64 bytes
    bool revoked = false;
    std::int64_t last_seen_unix = 0;
};

class PeerList {
public:
    /// Nullopt when the bytes are not a peer list of a known version.
    [[nodiscard]] static std::optional<PeerList> decode(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::vector<std::uint8_t> encode() const;

    /// Trusted, unknown or revoked (revoked peers are refused, never asked to pair again).
    [[nodiscard]] osplay_trust trust(const PublicKey& key) const;
    /// Adds the peer as trusted, or renames and un-revokes it.
    void trust(const PublicKey& key, const std::string& name, std::int64_t now_unix);
    /// False when the peer is not in the list.
    bool revoke(const PublicKey& key);
    bool remove(const PublicKey& key);
    void seen(const PublicKey& key, std::int64_t now_unix);

    [[nodiscard]] const std::vector<Peer>& peers() const { return peers_; }

private:
    Peer* find(const PublicKey& key);
    std::vector<Peer> peers_;
};

/// The protocol's 64-byte limit, cut at a character boundary.
[[nodiscard]] std::string clamp_peer_name(const std::string& name);

} // namespace osplay::engine
