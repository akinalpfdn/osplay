// Fuzz entry point for the protocol decoders. Properties checked on any input (run under
// ASan/UBSan): nothing crashes or reads out of bounds, and whatever decodes re-encodes to exactly
// the same bytes (encoding is canonical).
#include "protocol/constants.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/media_packets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);

    if (const auto message = osplay::protocol::decode_control(input)) {
        const auto encoded = osplay::protocol::encode_control(message.value());
        if (!encoded || !std::ranges::equal(encoded.value(), input)) {
            std::abort();
        }
    }

    if (const auto packet = osplay::protocol::decode_media(input)) {
        std::array<std::uint8_t, osplay::protocol::kMaxMediaPlaintextSize> buffer{};
        const auto written = osplay::protocol::encode_media(packet.value(), buffer);
        if (!written || !std::ranges::equal(std::span(buffer).first(written.value()), input)) {
            std::abort();
        }
    }

    return 0;
}
