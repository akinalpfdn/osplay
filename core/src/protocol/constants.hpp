#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Wire constants from docs/PROTOCOL.md. Changing any of these changes the protocol.
namespace osplay::protocol {

inline constexpr std::array<std::uint8_t, 2> kMagic{'O', 'P'};
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::uint8_t kMinorVersion = 0;

inline constexpr std::size_t kMaxControlMessageSize = 4096;
inline constexpr std::size_t kControlHeaderSize = 4; // magic, version, type

inline constexpr std::size_t kMaxDatagramSize = 1200;
inline constexpr std::size_t kAeadTagSize = 16;
inline constexpr std::size_t kMediaHeaderSize = 12; // magic, version, type, sequence
inline constexpr std::size_t kMaxMediaPlaintextSize = kMaxDatagramSize - kAeadTagSize;
inline constexpr std::size_t kVideoFragmentFieldsSize = 18;
inline constexpr std::size_t kMaxVideoPayload =
    kMaxMediaPlaintextSize - kMediaHeaderSize - kVideoFragmentFieldsSize;
inline constexpr std::size_t kCursorShapeFieldsSize = 17;
inline constexpr std::size_t kMaxCursorShapePayload =
    kMaxMediaPlaintextSize - kMediaHeaderSize - kCursorShapeFieldsSize;
static_assert(kMaxVideoPayload == 1154 && kMaxCursorShapePayload == 1155,
              "documented in PROTOCOL.md");

inline constexpr std::size_t kMaxStringBytes = 64;
inline constexpr std::size_t kSessionTokenSize = 16;
inline constexpr std::size_t kMaxHandshakePayload = 1024;
inline constexpr std::size_t kMaxMonitors = 4;
inline constexpr std::size_t kMaxNackRanges = 32;
inline constexpr std::uint8_t kMaxStreams = 4;
inline constexpr std::uint32_t kMaxFragmentsPerFrame = 4096;
inline constexpr std::uint16_t kMaxCursorDimension = 256;

} // namespace osplay::protocol
