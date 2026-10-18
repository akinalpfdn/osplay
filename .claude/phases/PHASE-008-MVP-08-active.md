# Phase 008 (MVP-08) — macOS viewer pipeline
Status: ACTIVE

DEVPLAN reference: Phase 7. Depends on: Phase 6.

## Goal
The Mac can receive and display a stream full screen on its built-in screen with minimal latency.

## Tasks
- [x] `VideoDecoding` with VideoToolbox: format description from in-band SPS/PPS, Annex B → AVCC, decode errors trigger keyframe requests.
- [x] `FramePresenting` with `AVSampleBufferDisplayLayer` (display-immediately) in a borderless window covering the target screen, menu bar and Dock hidden, cursor hidden when idle.
- [x] Cursor overlay layer for Windows-host cursor packets.
- [x] Exit hotkey and power assertion.
- [x] Debug-only loopback: the Mac hosts a virtual display and views it in a window on itself.

## Acceptance Criteria
- [x] Loopback on the Mac shows the virtual display content smoothly at 60 fps.
- [x] Hotkey always exits full screen, even when frames stop arriving.
- [x] Decoder recovers from injected corruption via IDR within one second.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision: default viewer hotkey is ⌃⌥⌘V (toggle); configurable in Phase 11.
- Core: cursor shapes/positions end to end; lost shapes recovered by keyframe request; viewer keyframe request API.
- Full-screen viewer is a borderless window above the menu bar (no Space switch).
- Measured (ViewerLoopbackHardwareTests, M3 Air): 59.1 fps presented, recovery from corruption 14.7 ms.
