#include "capi/random_source.hpp"
#include "crypto/noise.hpp"

#include <osplay/osplay.h>

#include <algorithm>
#include <span>

namespace {

static_assert(OSPLAY_KEY_SIZE == osplay::crypto::kKeySize);
static_assert(OSPLAY_DEVICE_ID_SIZE == 16);

osplay_result to_result(osplay::Error error) {
    return static_cast<osplay_result>(error);
}

osplay::crypto::Key key_from(const std::uint8_t* bytes) {
    osplay::crypto::Key key{};
    std::copy_n(bytes, key.size(), key.begin());
    return key;
}

void write_key(const osplay::crypto::Key& key, std::uint8_t* out) {
    std::ranges::copy(key, out);
}

} // namespace

extern "C" {

osplay_result osplay_identity_generate(const osplay_random_source* random, uint8_t* secret_key,
                                       uint8_t* public_key) {
    if (random == nullptr || random->fill == nullptr || secret_key == nullptr ||
        public_key == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    osplay::capi::CallbackRandomSource source(*random);
    const auto pair = osplay::crypto::KeyPair::generate(source);
    if (!pair) {
        return to_result(pair.error());
    }
    write_key(pair.value().secret(), secret_key);
    write_key(pair.value().public_key(), public_key);
    return OSPLAY_OK;
}

osplay_result osplay_identity_public_key(const uint8_t* secret_key, uint8_t* public_key) {
    if (secret_key == nullptr || public_key == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    osplay::crypto::Key secret = key_from(secret_key);
    const auto pair = osplay::crypto::KeyPair::from_secret(secret);
    osplay::crypto::wipe(secret);
    if (!pair) {
        return to_result(pair.error());
    }
    write_key(pair.value().public_key(), public_key);
    return OSPLAY_OK;
}

osplay_result osplay_identity_device_id(const uint8_t* public_key, uint8_t* device_id) {
    if (public_key == nullptr || device_id == nullptr) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    const auto id = osplay::crypto::device_id(key_from(public_key));
    std::ranges::copy(id, device_id);
    return OSPLAY_OK;
}

} // extern "C"
