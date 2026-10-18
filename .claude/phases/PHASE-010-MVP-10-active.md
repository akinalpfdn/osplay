# Phase 010 (MVP-10) — Windows viewer pipeline
Status: ACTIVE

DEVPLAN reference: Phase 9. Depends on: Phase 8.

## Goal
Windows can present up to two streams full screen on chosen physical monitors.

## Tasks
- [ ] MF hardware decoder with D3D11 output and low latency.
- [ ] Per-monitor borderless presentation windows with flip-model swap chains on a dedicated thread.
- [ ] Monitor enumeration (pixel size, refresh, physical size, DPI) exposed through the C API for the ViewerMonitors message.
- [ ] Hide/show hotkey, display-required power state.
- [ ] Debug loopback: Windows hosts a VDD monitor and views it on a physical monitor.

## Acceptance Criteria
- [ ] Two streams render simultaneously on two monitors with independent flip active (verified with PresentMon).
- [ ] Hide/show toggles instantly; hidden viewers tell the host to pause.
- [ ] Unplugging one monitor mid-session stops only that stream and informs the host.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
