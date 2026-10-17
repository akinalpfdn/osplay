#pragma once

#include "crypto/primitives.hpp"

#include <osplay/osplay.h>

namespace osplay::capi {

/// Adapts the platform's C callback to crypto::RandomSource.
class CallbackRandomSource final : public crypto::RandomSource {
public:
    explicit CallbackRandomSource(const osplay_random_source& source) : source_(source) {}

    bool fill(std::span<std::uint8_t> out) override {
        return source_.fill != nullptr &&
               source_.fill(source_.context, out.data(), out.size()) == OSPLAY_OK;
    }

private:
    osplay_random_source source_;
};

} // namespace osplay::capi
