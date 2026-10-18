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
- [ ] Stable 60 fps stream at 2560×1664 on NVIDIA, AMD or Intel hardware (whichever the developer has; others recorded in a test matrix).
- [ ] Mode change, UAC prompt and lock/unlock do not crash the pipeline; streaming resumes afterwards.
- [ ] Cursor shapes (arrow, text beam, resize, custom app cursors) are transmitted correctly.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Test hardware: NVIDIA GPU (NVENC MFT); AMD/Intel recorded as untested.
- User decision: acceptance runs through a command-line host test tool (osplay-hosttest.exe), not a WPF debug screen.
- Portable HostOrchestrator / pointer shape conversion / H.264 keyframe repair in osplay_engine_logic (tested on every platform).
- Core: osplay_session_close (Bye without destroying) for components that do not own the session.
- Windows code pre-checked on the Mac with zig mingw headers + clang-tidy before pushing.
