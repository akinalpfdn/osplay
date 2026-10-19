#include "keystore/peer_list.hpp"

#include "viewer/monitor_layout.hpp"

#include <algorithm>
#include <utility>

namespace osplay::engine {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'O', 'S', 'P', 'K'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kMaxPeers = 256;

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool bytes(std::span<std::uint8_t> out) {
        if (bytes_.size() - offset_ < out.size()) {
            return false;
        }
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_), out.size(), out.begin());
        offset_ += out.size();
        return true;
    }
    bool u8(std::uint8_t& value) { return bytes(std::span(&value, 1)); }
    bool i64(std::int64_t& value) {
        std::array<std::uint8_t, 8> raw{};
        if (!bytes(raw)) {
            return false;
        }
        std::uint64_t bits = 0;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            bits |= static_cast<std::uint64_t>(raw.at(i)) << (8U * i);
        }
        value = static_cast<std::int64_t>(bits);
        return true;
    }
    [[nodiscard]] bool done() const { return offset_ == bytes_.size(); }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

void put_i64(std::vector<std::uint8_t>& out, std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>(bits >> (8U * i)));
    }
}

} // namespace

std::string clamp_peer_name(const std::string& name) {
    return protocol_name(name);
}

std::optional<PeerList> PeerList::decode(std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    std::array<std::uint8_t, 4> magic{};
    std::uint8_t version = 0;
    std::array<std::uint8_t, 2> count_bytes{};
    if (!reader.bytes(magic) || magic != kMagic || !reader.u8(version) || version != kVersion ||
        !reader.bytes(count_bytes)) {
        return std::nullopt;
    }
    const std::size_t count = count_bytes[0] | (static_cast<std::size_t>(count_bytes[1]) << 8U);
    if (count > kMaxPeers) {
        return std::nullopt;
    }
    PeerList list;
    for (std::size_t i = 0; i < count; ++i) {
        Peer peer;
        std::uint8_t flags = 0;
        std::uint8_t name_length = 0;
        if (!reader.bytes(peer.public_key) || !reader.u8(flags) ||
            !reader.i64(peer.last_seen_unix) || !reader.u8(name_length)) {
            return std::nullopt;
        }
        std::string name(name_length, '\0');
        if (!reader.bytes(std::span(reinterpret_cast<std::uint8_t*>(name.data()), name.size()))) {
            return std::nullopt;
        }
        peer.revoked = (flags & 1U) != 0;
        peer.name = clamp_peer_name(name);
        list.peers_.push_back(std::move(peer));
    }
    return reader.done() ? std::optional(std::move(list)) : std::nullopt;
}

std::vector<std::uint8_t> PeerList::encode() const {
    std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end());
    out.push_back(kVersion);
    out.push_back(static_cast<std::uint8_t>(peers_.size() & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(peers_.size() >> 8U));
    for (const Peer& peer : peers_) {
        out.insert(out.end(), peer.public_key.begin(), peer.public_key.end());
        out.push_back(peer.revoked ? 1U : 0U);
        put_i64(out, peer.last_seen_unix);
        out.push_back(static_cast<std::uint8_t>(peer.name.size()));
        out.insert(out.end(), peer.name.begin(), peer.name.end());
    }
    return out;
}

osplay_trust PeerList::trust(const PublicKey& key) const {
    const auto it = std::ranges::find(peers_, key, &Peer::public_key);
    if (it == peers_.end()) {
        return OSPLAY_TRUST_UNKNOWN;
    }
    return it->revoked ? OSPLAY_TRUST_BLOCKED : OSPLAY_TRUST_TRUSTED;
}

void PeerList::trust(const PublicKey& key, const std::string& name, std::int64_t now_unix) {
    if (Peer* peer = find(key)) {
        peer->name = clamp_peer_name(name);
        peer->revoked = false;
        peer->last_seen_unix = now_unix;
        return;
    }
    if (peers_.size() < kMaxPeers) {
        peers_.push_back(Peer{.public_key = key,
                              .name = clamp_peer_name(name),
                              .revoked = false,
                              .last_seen_unix = now_unix});
    }
}

bool PeerList::revoke(const PublicKey& key) {
    Peer* peer = find(key);
    if (peer == nullptr) {
        return false;
    }
    peer->revoked = true;
    return true;
}

bool PeerList::remove(const PublicKey& key) {
    return std::erase_if(peers_, [&](const Peer& peer) { return peer.public_key == key; }) > 0;
}

void PeerList::seen(const PublicKey& key, std::int64_t now_unix) {
    if (Peer* peer = find(key)) {
        peer->last_seen_unix = now_unix;
    }
}

Peer* PeerList::find(const PublicKey& key) {
    const auto it = std::ranges::find(peers_, key, &Peer::public_key);
    return it == peers_.end() ? nullptr : &*it;
}

} // namespace osplay::engine
