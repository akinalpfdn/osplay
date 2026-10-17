#pragma once

#include <chrono>

namespace osplay {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;

} // namespace osplay
