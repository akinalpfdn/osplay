#pragma once

#include <cstdint>
#include <span>

namespace osplay::protocol {

/// Valid UTF-8 (no overlong forms, surrogates or values above U+10FFFF) without control
/// characters (U+0000–U+001F, U+007F–U+009F), as PROTOCOL.md requires for every string.
[[nodiscard]] bool is_valid_text(std::span<const std::uint8_t> bytes);

} // namespace osplay::protocol
