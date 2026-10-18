# Phase 008 (MVP-08) — macOS viewer pipeline
Status: ACTIVE

DEVPLAN reference: Phase 7. Depends on: Phase 6.

## Goal
The Mac can receive and display a stream full screen on its built-in screen with minimal latency.

## Tasks
- [ ] `VideoDecoding` with VideoToolbox: format description from in-band SPS/PPS, Annex B → AVCC, decode errors trigger keyframe requests.
- [ ] `FramePresenting` with `AVSampleBufferDisplayLayer` (display-immediately) in a borderless window covering the target screen, menu bar and Dock hidden, cursor hidden when idle.
- [ ] Cursor overlay layer for Windows-host cursor packets.
- [ ] Exit hotkey and power assertion.
- [ ] Debug-only loopback: the Mac hosts a virtual display and views it in a window on itself.

## Acceptance Criteria
- [ ] Loopback on the Mac shows the virtual display content smoothly at 60 fps.
- [ ] Hotkey always exits full screen, even when frames stop arriving.
- [ ] Decoder recovers from injected corruption via IDR within one second.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision: default viewer hotkey is ⌃⌥⌘V (toggle); configurable in Phase 11.
