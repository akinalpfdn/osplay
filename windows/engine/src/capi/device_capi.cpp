#include "../discovery/dns_sd_discovery.hpp"
#include "../keystore/dpapi_key_store.hpp"
#include "engine_results.hpp"
#include "session_trust.hpp"

#include <osplay/engine.h>
#include <osplay/osplay.h>

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <span>
#include <string>

namespace {

using osplay::engine::DpapiKeyStore;
using osplay::engine::guarded;
using osplay::engine::PeerList;
using osplay::engine::PublicKey;
using osplay::engine::to_result;

PublicKey key_of(const uint8_t* bytes) {
    PublicKey key{};
    std::copy_n(bytes, key.size(), key.begin());
    return key;
}

/// Copies `text` and a NUL into `out`, cut to fit.
template <std::size_t Size>
void copy_text(const std::string& text,
               char (&out)[Size]) { // NOLINT(*-avoid-c-arrays): C API struct field
    const std::size_t length = std::min(text.size(), Size - 1);
    std::memcpy(static_cast<char*>(out), text.data(), length);
    out[length] = '\0'; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index): length < Size
}

osplay_engine_result change_peer(const uint8_t* public_key, bool revoke) {
    if (public_key == nullptr) {
        return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
    }
    const PublicKey key = key_of(public_key);
    bool known = false;
    const auto failure = DpapiKeyStore::shared().update_peers(
        [&](PeerList& list) { known = revoke ? list.revoke(key) : list.remove(key); });
    if (failure) {
        return to_result(*failure);
    }
    if (known) {
        osplay::engine::SessionTrust::peers_changed(key,
                                                    true); // refused from now, also mid-session
    }
    return known ? OSPLAY_ENGINE_OK : OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
}

} // namespace

struct osplay_engine_browser {
    std::unique_ptr<osplay::engine::DnsSdBrowser> browser;
};

struct osplay_engine_advertiser {
    std::unique_ptr<osplay::engine::DnsSdAdvertiser> advertiser;
};

extern "C" {

osplay_engine_result osplay_engine_identity(uint8_t* out_public_key, uint8_t* out_device_id) {
    return guarded([&] {
        if (out_public_key == nullptr || out_device_id == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        auto identity = DpapiKeyStore::shared().identity();
        if (!identity) {
            return to_result(identity.error());
        }
        SecureZeroMemory(identity.value().secret_key.data(), identity.value().secret_key.size());
        std::ranges::copy(identity.value().public_key, out_public_key);
        return osplay_identity_device_id(out_public_key, out_device_id) == OSPLAY_OK
                   ? OSPLAY_ENGINE_OK
                   : OSPLAY_ENGINE_ERROR_KEY_STORE_DAMAGED;
    });
}

osplay_engine_result osplay_engine_peers(osplay_engine_peer* out_peers, size_t capacity,
                                         size_t* out_count) {
    return guarded([&] {
        if (out_count == nullptr || (out_peers == nullptr && capacity != 0)) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const auto list = DpapiKeyStore::shared().peers();
        if (!list) {
            return to_result(list.error());
        }
        const auto& peers = list.value().peers();
        const std::span out(out_peers, capacity);
        for (std::size_t i = 0; i < std::min(capacity, peers.size()); ++i) {
            osplay_engine_peer& target = out[i];
            target = osplay_engine_peer{};
            std::ranges::copy(peers[i].public_key, std::begin(target.public_key));
            copy_text(peers[i].name, target.name_utf8);
            target.revoked = peers[i].revoked ? 1 : 0;
            target.last_seen_unix = peers[i].last_seen_unix;
        }
        *out_count = peers.size();
        return OSPLAY_ENGINE_OK;
    });
}

osplay_engine_result osplay_engine_peer_revoke(const uint8_t* public_key) {
    return guarded([&] { return change_peer(public_key, true); });
}

osplay_engine_result osplay_engine_peer_remove(const uint8_t* public_key) {
    return guarded([&] { return change_peer(public_key, false); });
}

osplay_engine_result osplay_engine_browser_start(const osplay_engine_browser_callbacks* callbacks,
                                                 osplay_engine_browser** out_browser) {
    return guarded([&] {
        if (out_browser == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        *out_browser = nullptr;
        if (callbacks == nullptr || callbacks->on_found == nullptr ||
            callbacks->on_lost == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        const osplay_engine_browser_callbacks shell = *callbacks;
        auto browser = osplay::engine::DnsSdBrowser::start({
            .on_found =
                [shell](const osplay::engine::DiscoveredDevice& found) {
                    osplay_engine_device device{.label = {},
                                                .advertisement = found.advertisement,
                                                .address_utf8 = {},
                                                .port = found.port};
                    copy_text(found.label, device.label);
                    copy_text(found.address, device.address_utf8);
                    shell.on_found(shell.context, &device);
                },
            .on_lost =
                [shell](const std::string& label) { shell.on_lost(shell.context, label.c_str()); },
        });
        if (!browser) {
            return OSPLAY_ENGINE_ERROR_DISCOVERY_UNAVAILABLE;
        }
        auto owned = std::make_unique<osplay_engine_browser>(
            osplay_engine_browser{std::move(browser.value())});
        *out_browser = owned.release(); // the caller owns it until _destroy
        return OSPLAY_ENGINE_OK;
    });
}

void osplay_engine_browser_destroy(osplay_engine_browser* browser) {
    const std::unique_ptr<osplay_engine_browser> owned(browser); // taking ownership back
}

osplay_engine_result osplay_engine_advertiser_start(const char* name_utf8, uint8_t roles,
                                                    uint16_t port,
                                                    osplay_engine_advertiser** out_advertiser) {
    return guarded([&] {
        if (out_advertiser == nullptr) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        *out_advertiser = nullptr;
        if (name_utf8 == nullptr || port == 0) {
            return OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT;
        }
        std::array<std::uint8_t, OSPLAY_KEY_SIZE> public_key{};
        osplay_advertisement advertisement = osplay::engine::blank_advertisement();
        if (const osplay_engine_result identity =
                osplay_engine_identity(public_key.data(), std::begin(advertisement.device_id));
            identity != OSPLAY_ENGINE_OK) {
            return identity;
        }
        advertisement.protocol_version = OSPLAY_PROTOCOL_VERSION;
        advertisement.platform = OSPLAY_PLATFORM_WINDOWS;
        advertisement.roles = roles;
        copy_text(name_utf8, advertisement.name);
        auto advertiser = osplay::engine::DnsSdAdvertiser::start(advertisement, port);
        if (!advertiser) {
            return advertiser.error() == osplay::engine::DiscoveryError::InvalidAdvertisement
                       ? OSPLAY_ENGINE_ERROR_INVALID_ARGUMENT
                       : OSPLAY_ENGINE_ERROR_DISCOVERY_UNAVAILABLE;
        }
        auto owned = std::make_unique<osplay_engine_advertiser>(
            osplay_engine_advertiser{std::move(advertiser.value())});
        *out_advertiser = owned.release(); // the caller owns it until _destroy
        return OSPLAY_ENGINE_OK;
    });
}

void osplay_engine_advertiser_destroy(osplay_engine_advertiser* advertiser) {
    const std::unique_ptr<osplay_engine_advertiser> owned(advertiser); // taking ownership back
}

} // extern "C"
