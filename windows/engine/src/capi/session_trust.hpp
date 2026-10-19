#pragma once

// Trust for engine sessions started with the stored identity: answers the core's trust queries from
// a copy of the paired devices, and stores a pairing once both users accepted it, under the name
// the peer introduced itself with. Every such session is registered, so a pairing, revoke or
// removal refreshes all copies at once, and revoking or removing the device a session is talking
// to ends that session. Recording "last seen" rewrites the small store file once per connection, a
// few milliseconds of the handshake.

#include "../keystore/dpapi_key_store.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace osplay::engine {

class SessionTrust {
public:
    SessionTrust() = default;
    ~SessionTrust() {
        try {
            const std::scoped_lock lock(registry_mutex());
            std::erase(registry(), this);
        } catch (...) {
            std::terminate(); // a registered pointer must never outlive the session
        }
    }
    SessionTrust(const SessionTrust&) = delete;
    SessionTrust& operator=(const SessionTrust&) = delete;
    SessionTrust(SessionTrust&&) = delete;
    SessionTrust& operator=(SessionTrust&&) = delete;

    /// Called when the session's peer is revoked or removed; must not block (it runs on the
    /// caller's thread with the registry locked).
    void set_end_session(std::function<void()> end_session) {
        const std::scoped_lock lock(mutex_);
        end_session_ = std::move(end_session);
    }

    /// The paired devices changed (a pairing, revoke or removal): every registered session reloads
    /// them, and the session talking to `key` ends when `end_sessions` is set.
    static void peers_changed(const PublicKey& key, bool end_sessions) {
        const std::scoped_lock lock(registry_mutex());
        for (SessionTrust* session : registry()) {
            session->reload_and_check(key, end_sessions);
        }
    }

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
        {
            const std::scoped_lock lock(registry_mutex());
            registry().push_back(this);
        }
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
        peer_ = key;
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

    /// After a pairing is stored (outside this object's lock): other sessions see it too.
    static void paired_stored(const std::uint8_t* public_key) {
        peers_changed(key_of(public_key), false);
    }

private:
    static std::vector<SessionTrust*>& registry() {
        static std::vector<SessionTrust*> sessions;
        return sessions;
    }
    static std::mutex& registry_mutex() {
        static std::mutex mutex;
        return mutex;
    }

    void reload_and_check(const PublicKey& key, bool end_sessions) {
        auto peers = DpapiKeyStore::shared().peers();
        std::function<void()> end;
        {
            const std::scoped_lock lock(mutex_);
            if (peers) {
                peers_ = std::move(peers.value());
            }
            if (end_sessions && peer_ == key) {
                end = end_session_;
            }
        }
        if (end) {
            end();
        }
    }

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
    std::optional<PublicKey> peer_; ///< the key of the current session's peer, once known
    std::function<void()> end_session_;
};

} // namespace osplay::engine
