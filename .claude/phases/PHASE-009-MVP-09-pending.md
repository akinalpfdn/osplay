# Phase 009 (MVP-09) — Windows host media pipeline
Status: PENDING

DEVPLAN reference: Phase 8. Depends on: Phase 2, Phase 5.

## Goal
Windows captures its virtual monitor and produces an encoded stream and cursor stream into `core`.

## Tasks
- [ ] Desktop Duplication capture on the VDD monitor's adapter with access-lost recovery.
- [ ] GPU BGRA→NV12 conversion and MF hardware H.264 encoder in async mode with low-latency settings and forced IDR.
- [ ] Pointer shape conversion (monochrome, color, masked color → BGRA) and cursor packets.
- [ ] Host session orchestration in the engine.
- [ ] Debug `.h264` dump.

## Acceptance Criteria
- [ ] Stable 60 fps stream at 2560×1664 on NVIDIA, AMD or Intel hardware (whichever the developer has; others recorded in a test matrix).
- [ ] Mode change, UAC prompt and lock/unlock do not crash the pipeline; streaming resumes afterwards.
- [ ] Cursor shapes (arrow, text beam, resize, custom app cursors) are transmitted correctly.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
