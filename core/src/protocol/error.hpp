#pragma once

#include <cstdint>

namespace osplay::protocol {

/// Protocol failures. Values equal the osplay_result codes of the C API (docs/PROTOCOL.md).
enum class Error : std::uint8_t {
    InvalidArgument = 1,
    BufferTooSmall = 2,
    Truncated = 3,
    BadMagic = 4,
    UnsupportedVersion = 5,
    UnknownType = 6,
    TrailingBytes = 7,
    ValueOutOfRange = 8,
    InvalidString = 9,
    TooLarge = 10,
};

} // namespace osplay::protocol
