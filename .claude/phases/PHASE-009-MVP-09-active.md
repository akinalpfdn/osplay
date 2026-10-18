# Phase 009 (MVP-09) — Windows host media pipeline
Status: ACTIVE

DEVPLAN reference: Phase 8. Depends on: Phase 2, Phase 5.

## Goal
Windows captures its virtual monitor and produces an encoded stream and cursor stream into `core`.

## Tasks
- [x] Desktop Duplication capture on the VDD monitor's adapter with access-lost recovery.
- [x] GPU BGRA→NV12 conversion and MF hardware H.264 encoder in async mode with low-latency settings and forced IDR.
- [x] Pointer shape conversion (monochrome, color, masked color → BGRA) and cursor packets.
- [x] Host session orchestration in the engine.
- [x] Debug `.h264` dump.

## Acceptance Criteria
- [x] Stable 60 fps stream at 2560×1664 on NVIDIA, AMD or Intel hardware (whichever the developer has; others recorded in a test matrix).
- [x] Mode change, UAC prompt and lock/unlock do not crash the pipeline; streaming resumes afterwards.
- [x] Cursor shapes (arrow, text beam, resize, custom app cursors) are transmitted correctly.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Test hardware: NVIDIA GPU (NVENC MFT); AMD/Intel recorded as untested.
- User decision: acceptance runs through a command-line host test tool (osplay-hosttest.exe), not a WPF debug screen.
- Portable HostOrchestrator / pointer shape conversion / H.264 keyframe repair in osplay_engine_logic (tested on every platform).
- Core: osplay_session_close (Bye without destroying) for components that do not own the session.
- Windows code pre-checked on the Mac with zig mingw headers + clang-tidy before pushing.
- PC run 1 (NVIDIA): stream decodes cleanly, 25 cursor shapes correct, UAC/lock recover; but capture ran at 65–104 fps (DWM composes faster than 60 Hz) → FramePacer caps encoding to one frame per interval on a fixed cadence.
- Mode change cannot be done in Settings (VDD gets one mode): test with orientation/scale change of the virtual monitor or a primary-monitor mode change instead.
- Animated cursors resend full shapes (~80 KB/s): acceptable; a viewer-side shape cache would need a protocol change (deferred).
- PC run 2: 59–62 fps; orientation change survived without a gap; p99 occasionally 33.5–35 ms → 1 ms timer resolution (timeBeginPeriod + power-throttling opt-out) on the pipeline thread.
- PC run 3: 60.0 fps steady, p99 19–27 ms, max ≤ 30 ms, latency ~29 ms. Acceptance met.
