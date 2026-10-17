# Phase 007 (MVP-07) — macOS host media pipeline
Status: PENDING

DEVPLAN reference: Phase 6. Depends on: Phase 1, Phase 5.

## Goal
The Mac captures its virtual displays and produces encoded streams into `core`.

## Tasks
- [ ] `ScreenCapturing` with ScreenCaptureKit per virtual display: pixel size from the stream config, 420v pixel format straight into the encoder, cursor composited, idle frame handling.
- [ ] `VideoEncoding` with VideoToolbox: realtime, low-latency rate control, no reordering, forced IDR on request, AVCC → Annex B conversion with in-band parameter sets.
- [ ] `HostSession` actor wiring displays, capture, encoders and the core session.
- [ ] Debug option to dump a stream to an `.h264` file for inspection.

## Acceptance Criteria
- [ ] Two simultaneous 2560×1440 at 60 Hz streams run with stable frame pacing.
- [ ] Dumped files play correctly in a standard player.
- [ ] Revoking Screen Recording permission mid-session stops cleanly with a surfaced error.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
