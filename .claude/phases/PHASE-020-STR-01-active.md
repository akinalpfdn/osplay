# Phase 020 (STR-01) — Stream resolution
Status: ACTIVE

DEVPLAN reference: Phase 13c. Runs after PHASE-018 (SYM-02) and before PHASE-019 (SYM-03) — user decision 2026-10-25.

## Goal
Each stream is encoded at most at a chosen size (1080p by default) while the virtual display keeps the viewer monitor's resolution, so several large displays stay usable on a modest host and on Wi-Fi.

## Why
TESTING.md D4 on 2026-10-25: an M1 MacBook host extending onto a MacBook at 3420×2214 and a 4K monitor produced 0–36 fps per stream, resent up to 1,275 fragments and asked for over 150 keyframes in four minutes. Both displays were unusable. Making the virtual display 1080p instead would make everything on it bigger, which the user rejected.

## Tasks
- [x] Stream size setting on the extending computer, next to quality (Mac menu, Windows panel): 720p, 1080p (default), 1440p, Native. The stream fits inside that box with the monitor's aspect ratio (3420×2214 → 1668×1080), with even sides, and is never larger than the monitor. Applies from the next connection, like quality.
- [x] Mac host: capture and encode at the stream size; the virtual display keeps the monitor's resolution.
- [x] Windows host: scale the Desktop Duplication frame to the stream size on the GPU before encoding; the virtual monitor keeps its mode.
- [x] Bitrate follows the stream size (quality preset × stream pixels × frame rate).
- [x] Viewers show the stream scaled to the whole display (aspect fit already); check that cursors and the stats overlay follow.
- [x] Rate-based pacing in core: a frame leaves no faster than about 2.5× its stream's bitrate target, instead of within half a frame interval, so a large keyframe no longer leaves in one short burst.
- [x] Settings persisted on both platforms; English and Turkish strings; theme tokens.
- [x] Tests: size fitting, bitrate, pacing rate; PROTOCOL.md, ARCHITECTURE.md and TESTING.md updated.

## Acceptance Criteria
- [x] D4 again (M1 host → MacBook at 3420×2214 plus a 4K monitor, over Wi-Fi) at 1080p: both displays usable near 60 fps, no keyframe storm. (2026-10-25: host at 55–60 fps; the remaining loss was AWDL — zero with awdl0 down, halved by #27, the rest is issue #28.)
- [ ] Native behaves as today.
- [x] A Windows host does the same with two large viewer monitors. (2026-10-25, D2 on the RTX 3080 PC at 1080p, after giving every virtual monitor its own driver device; two on one device stalled the whole desktop.)

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-25 — User decision: the stream size is chosen on the extending computer, 1080p by default.
- 2026-10-25 — User decision: no bandwidth warning and no network test.
- Stream sizes are computed once, in core (`osplay_fit_stream_size`), so both hosts give a monitor the same stream; the longer side is fitted against the box's longer side, so portrait monitors work.
- Windows hosts scale the cursor shape with the stream; Mac hosts composite the cursor into the video, which scales with it.
- Pacing rate is 2.5 × the bitrate targets of all streams together, with a 100 ms queue cap, after libwebrtc's pacer.

