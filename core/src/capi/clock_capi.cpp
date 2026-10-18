#include "util/time.hpp"

#include <osplay/osplay.h>

#include <chrono>

extern "C" uint64_t osplay_clock_microseconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<osplay::Microseconds>(osplay::Clock::now().time_since_epoch())
            .count());
}
