// Fuzz entry point for the cryptographic parsers: the Noise handshake reader and the media
// datagram opener. Properties checked on any input (run under ASan/UBSan): nothing crashes or reads
// out of bounds, and forged handshake messages and datagrams are never accepted.
#include "crypto/media_crypto.hpp"
#include "crypto/noise.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

namespace {

/// Fixed bytes: fuzzing must be deterministic and reproducible.
class FixedRandom final : public osplay::crypto::RandomSource {
public:
    bool fill(std::span<std::uint8_t> out) override {
        for (std::uint8_t& byte : out) {
            byte = static_cast<std::uint8_t>(++counter_);
        }
        return true;
    }

private:
    std::uint8_t counter_ = 0;
};

osplay::crypto::KeyPair fixed_pair(FixedRandom& random) {
    auto pair = osplay::crypto::KeyPair::generate(random);
    if (!pair) {
        std::abort();
    }
    return pair.value();
}

void fuzz_handshake(std::span<const std::uint8_t> input) {
    using osplay::crypto::HandshakeRole;
    using osplay::crypto::HandshakeState;
    FixedRandom random;
    const auto initiator_static = fixed_pair(random);
    const auto responder_static = fixed_pair(random);

    // As message 1 to a responder.
    HandshakeState responder(HandshakeRole::Responder, responder_static, {}, random);
    static_cast<void>(responder.read_message(input));

    // As message 2 to an initiator: random bytes cannot authenticate.
    HandshakeState initiator(HandshakeRole::Initiator, initiator_static, {}, random);
    if (!initiator.write_message({})) {
        std::abort();
    }
    if (initiator.read_message(input) && input.size() >= 96) {
        std::abort();
    }
}

void fuzz_media_opener(std::span<const std::uint8_t> input) {
    const osplay::crypto::Key key{1, 2, 3};
    osplay::crypto::MediaOpener opener(key);
    std::vector<std::uint8_t> datagram(input.begin(), input.end());
    if (opener.open(datagram)) {
        std::abort(); // a forgery under an unknown key
    }
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    fuzz_handshake(input);
    fuzz_media_opener(input);
    return 0;
}
