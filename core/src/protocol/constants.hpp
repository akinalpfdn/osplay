#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Wire constants from docs/PROTOCOL.md. Changing any of these changes the protocol.
namespace osplay::protocol {

inline constexpr std::array<std::uint8_t, 2> kMagic{'O', 'P'};
inline constexpr std::uint8_t kVersion = 1;
// 1.1 added role swap; 1.2 added the iPadOS and Android platforms and the "pm" TXT key; 1.3 added
// audio (the "plays audio" capability, AudioConfig, AudioStop and AudioPacket).
inline constexpr std::uint8_t kMinorVersion = 3;

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
inline constexpr std::size_t kAudioPacketFieldsSize = 12;
inline constexpr std::size_t kMaxAudioPayload =
    kMaxMediaPlaintextSize - kMediaHeaderSize - kAudioPacketFieldsSize;
static_assert(kMaxVideoPayload == 1154 && kMaxCursorShapePayload == 1155 &&
                  kMaxAudioPayload == 1160,
              "documented in PROTOCOL.md");

inline constexpr std::size_t kMaxStringBytes = 64;
inline constexpr std::size_t kSessionTokenSize = 16;
inline constexpr std::size_t kMaxHandshakePayload = 1024;
inline constexpr std::size_t kMaxMonitors = 4;
inline constexpr std::size_t kMaxNackRanges = 32;
inline constexpr std::uint8_t kMaxStreams = 4;
inline constexpr std::uint32_t kMaxFragmentsPerFrame = 4096;
inline constexpr std::uint16_t kMaxCursorDimension = 256;
inline constexpr std::uint32_t kAudioSampleRate = 48'000;
inline constexpr std::uint16_t kMinAudioBitrateKbps = 6;
inline constexpr std::uint16_t kMaxAudioBitrateKbps = 510;

} // namespace osplay::protocol
