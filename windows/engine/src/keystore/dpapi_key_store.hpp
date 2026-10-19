#pragma once

// The device identity and the peer list in %APPDATA%\Osplay, each encrypted with DPAPI for the
// current user (DEVPLAN: keystore). Thread-safe; one process-wide instance.

#include "keystore/peer_list.hpp"

#include <osplay/win/expected.hpp>

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace osplay::engine {

enum class KeyStoreError : std::uint8_t {
    Unavailable, ///< the folder cannot be created or the file cannot be read or written
    Damaged,     ///< a file exists but cannot be decrypted or parsed
    RandomUnavailable,
};

class DpapiKeyStore {
public:
    static DpapiKeyStore& shared();

    struct Identity {
        std::array<std::uint8_t, OSPLAY_KEY_SIZE> secret_key{};
        PublicKey public_key{};
    };

    /// Loads the identity, creating it on first use.
    [[nodiscard]] win::Expected<Identity, KeyStoreError> identity();

    [[nodiscard]] win::Expected<PeerList, KeyStoreError> peers();
    /// Applies `change` to the stored list and writes it back.
    template <typename Change>
    [[nodiscard]] std::optional<KeyStoreError> update_peers(const Change& change) {
        const std::scoped_lock lock(mutex_);
        auto list = load_peers();
        if (!list) {
            return list.error();
        }
        change(list.value());
        return save_peers(list.value());
    }

private:
    // Called with `mutex_` held.
    static win::Expected<PeerList, KeyStoreError> load_peers();
    static std::optional<KeyStoreError> save_peers(const PeerList& list);

    std::mutex mutex_;
};

} // namespace osplay::engine
