#pragma once

#include <osplay/osplay.h>

#include <windows.h>

#include <bcrypt.h>

#include <climits>
#include <cstddef>
#include <cstdint>

namespace osplay::engine {

/// osplay_random_source.fill backed by the system's preferred CSPRNG (DEVPLAN: BCryptGenRandom).
inline osplay_result system_random(void* /*context*/, std::uint8_t* buffer, std::size_t length) {
    if (buffer == nullptr || length > ULONG_MAX) {
        return OSPLAY_ERROR_INVALID_ARGUMENT;
    }
    return BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(length),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0
               ? OSPLAY_OK
               : OSPLAY_ERROR_RANDOM_UNAVAILABLE;
}

} // namespace osplay::engine
