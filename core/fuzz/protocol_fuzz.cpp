// Fuzz entry point for the protocol decoders. Properties checked on any input (run under
// ASan/UBSan): nothing crashes or reads out of bounds, and whatever decodes re-encodes to exactly
// the same bytes (encoding is canonical). The usbmuxd plist reader only has to survive.
#include "protocol/constants.hpp"
#include "protocol/control_messages.hpp"
#include "protocol/link_record.hpp"
#include "protocol/media_packets.hpp"
#include "transport/usbmux.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

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

    // Stream link records: the records read from a prefix of the input write back to that prefix.
    osplay::protocol::LinkRecordReader reader;
    reader.append(input);
    std::vector<std::uint8_t> rewritten;
    for (auto record = reader.next(); record && record.value(); record = reader.next()) {
        if (!osplay::protocol::append_link_record(record.value()->kind, record.value()->payload,
                                                  rewritten)) {
            std::abort();
        }
    }
    if (!std::ranges::equal(rewritten, input.first(rewritten.size()))) {
        std::abort();
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes viewed as text
    static_cast<void>(osplay::transport::usbmux::parse_message(
        std::string_view(reinterpret_cast<const char*>(data), size)));

    return 0;
}
