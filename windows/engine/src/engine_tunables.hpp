#pragma once

#include <cstdint>

namespace osplay::engine {

/// How long the engine waits for a newly enabled virtual monitor to join the desktop.
inline constexpr std::uint32_t kMonitorArrivalTimeoutMilliseconds = 5000;
inline constexpr std::uint32_t kMonitorArrivalPollMilliseconds = 100;
/// Topology can change between sizing and querying the display configuration.
inline constexpr int kDisplayConfigQueryAttempts = 3;

/// A Windows host sends one stream: its single virtual monitor.
inline constexpr std::uint8_t kHostStreamId = 0;
/// Target bitrate in bits per pixel per frame: 0.1 gives ~25 Mbit/s for 2560×1664 at 60 Hz.
inline constexpr double kBitsPerPixel = 0.1;
inline constexpr std::uint32_t kMinBitrateKbps = 2'000;
inline constexpr std::uint32_t kMaxBitrateKbps = 80'000;

} // namespace osplay::engine
