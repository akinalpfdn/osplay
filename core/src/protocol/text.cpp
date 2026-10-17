#include "protocol/text.hpp"

#include <array>
#include <cstddef>

namespace osplay::protocol {

bool is_valid_text(std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint32_t, 4> kSmallestForLength{0x0, 0x80, 0x800, 0x10000};
    std::size_t i = 0;
    while (i < bytes.size()) {
        const std::uint8_t lead = bytes[i];
        std::size_t extra = 0;
        std::uint32_t code_point = 0;
        if (lead < 0x80U) {
            code_point = lead;
        } else if ((lead & 0xE0U) == 0xC0U) {
            extra = 1;
            code_point = lead & 0x1FU;
        } else if ((lead & 0xF0U) == 0xE0U) {
            extra = 2;
            code_point = lead & 0x0FU;
        } else if ((lead & 0xF8U) == 0xF0U) {
            extra = 3;
            code_point = lead & 0x07U;
        } else {
            return false;
        }
        if (i + extra >= bytes.size() && extra > 0) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const std::uint8_t continuation = bytes[i + k];
            if ((continuation & 0xC0U) != 0x80U) {
                return false;
            }
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        const bool overlong = code_point < kSmallestForLength.at(extra);
        const bool surrogate = code_point >= 0xD800U && code_point <= 0xDFFFU;
        const bool control = code_point < 0x20U || (code_point >= 0x7FU && code_point < 0xA0U);
        if (overlong || surrogate || control || code_point > 0x10FFFFU) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

} // namespace osplay::protocol
