#pragma once

// Trust for engine sessions started with the stored identity: answers the core's trust queries from
// a copy of the paired devices loaded at start, and stores a pairing once both users accepted it,
// under the name the peer introduced itself with. Recording "last seen" rewrites the small store
// file once per connection, a few milliseconds of the handshake.

#include "../keystore/dpapi_key_store.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace osplay::engine {

class SessionTrust {
public:
    /// The secret key a session uses: `given` (the caller answers trust) or the stored identity
    /// (this object answers trust). Nullopt with `failure` set when the store cannot be used.
    std::optional<std::array<std::uint8_t, OSPLAY_KEY_SIZE>> secret_for(const std::uint8_t* given,
                                                                        KeyStoreError& failure) {
        std::array<std::uint8_t, OSPLAY_KEY_SIZE> secret{};
        if (given != nullptr) {
            std::copy_n(given, secret.size(), secret.begin());
            return secret;
        }
        auto identity = DpapiKeyStore::shared().identity();
        if (!identity) {
            failure = identity.error();
            return std::nullopt;
        }
        if (!load()) {
            failure = KeyStoreError::Damaged;
            return std::nullopt;
        }
        stored_ = true;
        return identity.value().secret_key;
    }

    /// True when trust comes from the paired devices.
    [[nodiscard]] bool stored() const { return stored_; }

    /// Loads the paired devices; false when the store cannot be read.
    bool load() {
        auto peers = DpapiKeyStore::shared().peers();
        if (!peers) {
            return false;
        }
        const std::scoped_lock lock(mutex_);
        peers_ = std::move(peers.value());
        return true;
    }

    void introduced(const char* device_name) {
        const std::scoped_lock lock(mutex_);
        peer_name_ = device_name == nullptr ? std::string() : std::string(device_name);
    }

    [[nodiscard]] osplay_trust trust(const std::uint8_t* public_key) {
        const PublicKey key = key_of(public_key);
        const std::scoped_lock lock(mutex_);
        const osplay_trust level = peers_.trust(key);
        if (level == OSPLAY_TRUST_TRUSTED) {
            static_cast<void>(DpapiKeyStore::shared().update_peers(
                [&](PeerList& list) { list.seen(key, now_unix()); })); // informational only
        }
        return level;
    }

    void paired(const std::uint8_t* public_key) {
        const PublicKey key = key_of(public_key);
        const std::scoped_lock lock(mutex_);
        const std::string name = peer_name_.empty() ? std::string("?") : peer_name_;
        peers_.trust(key, name, now_unix());
        // A failed write means pairing again next time; the session itself is unaffected.
        static_cast<void>(DpapiKeyStore::shared().update_peers(
            [&](PeerList& list) { list.trust(key, name, now_unix()); }));
    }

private:
    static PublicKey key_of(const std::uint8_t* bytes) {
        PublicKey key{};
        std::copy_n(bytes, key.size(), key.begin());
        return key;
    }
    static std::int64_t now_unix() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    bool stored_ = false; // set before the session starts, read-only afterwards
    std::mutex mutex_;
    PeerList peers_;
    std::string peer_name_;
};

} // namespace osplay::engine
