#pragma once

#include <windows.h>

#include <timeapi.h>

namespace osplay::engine {

/// 1 ms timer resolution for the lifetime of the object, process wide. Without it waits round up
/// to 15.6 ms: captured frames miss their slot, and the session's 2 ms tick (NACKs, pacing,
/// deadlines) runs at a fraction of its rate. Windows 11 ignores the request for processes
/// without visible windows unless they opt out of timer-resolution throttling.
class PrecisionTimerScope {
public:
    PrecisionTimerScope() : active_(enable()) {}
    ~PrecisionTimerScope() {
        if (active_) {
            static_cast<void>(timeEndPeriod(1));
        }
    }
    PrecisionTimerScope(const PrecisionTimerScope&) = delete;
    PrecisionTimerScope& operator=(const PrecisionTimerScope&) = delete;
    PrecisionTimerScope(PrecisionTimerScope&&) = delete;
    PrecisionTimerScope& operator=(PrecisionTimerScope&&) = delete;

private:
    static bool enable() {
        PROCESS_POWER_THROTTLING_STATE throttling{};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        throttling.StateMask = 0;
        // Best effort: older Windows versions refuse it and keep the coarse resolution.
        static_cast<void>(SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                                                &throttling,
                                                static_cast<DWORD>(sizeof(throttling))));
        return timeBeginPeriod(1) == TIMERR_NOERROR;
    }

    bool active_;
};

} // namespace osplay::engine
