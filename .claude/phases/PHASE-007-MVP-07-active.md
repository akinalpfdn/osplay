# Phase 007 (MVP-07) — macOS host media pipeline
Status: ACTIVE

DEVPLAN reference: Phase 6. Depends on: Phase 1, Phase 5.

## Goal
The Mac captures its virtual displays and produces encoded streams into `core`.

## Tasks
- [x] `ScreenCapturing` with ScreenCaptureKit per virtual display: pixel size from the stream config, 420v pixel format straight into the encoder, cursor composited, idle frame handling.
- [x] `VideoEncoding` with VideoToolbox: realtime, low-latency rate control, no reordering, forced IDR on request, AVCC → Annex B conversion with in-band parameter sets.
- [x] `HostSession` actor wiring displays, capture, encoders and the core session.
- [x] Debug option to dump a stream to an `.h264` file for inspection.

## Acceptance Criteria
- [x] Two simultaneous 2560×1440 at 60 Hz streams run with stable frame pacing.
- [x] Dumped files play correctly in a standard player.
- [x] Revoking Screen Recording permission mid-session stops cleanly with a surfaced error.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Keyframe requests re-encode the last captured frame (idle screens produce no frames).
- Low-latency rate control with a realtime fallback; only essential encoder properties are mandatory.
- Acceptance harness: debug "Host media test" streams over loopback into an in-process core viewer.
- Encoder creation stalls for minutes when the app runs from ~/Documents (sandbox extension); build outside it.
- Capture interval has 2× headroom; measured 59.1 fps, p99 21.5 ms for two 1440p60 streams (HostPipelineHardwareTests).
- Capture timestamps are converted to the core's clock (osplay_clock_microseconds); CMClock host time stops during sleep.
