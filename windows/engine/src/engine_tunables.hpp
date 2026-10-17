#pragma once

#include <cstdint>

namespace osplay::engine {

/// How long the engine waits for a newly enabled virtual monitor to join the desktop.
inline constexpr std::uint32_t kMonitorArrivalTimeoutMilliseconds = 5000;
inline constexpr std::uint32_t kMonitorArrivalPollMilliseconds = 100;
/// Topology can change between sizing and querying the display configuration.
inline constexpr int kDisplayConfigQueryAttempts = 3;

} // namespace osplay::engine
