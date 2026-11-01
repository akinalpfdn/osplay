# Phase 026 (TV-01) — Hosts ready for TV viewers
Status: ACTIVE

DEVPLAN reference: Phase 16 (part TV-01). Depends on: Phase 15.

## Goal
A Mac or a PC extends onto a TV-sized viewer (1080p or 4K, size unknown, watched from across the room) with a usable desktop scale and a stream the viewer can decode.

## Tasks
- [x] Hosts honour the viewer's Capabilities: today the host ignores `max_width`, `max_height` and `max_refresh_hz` (`session_engine.cpp` drops the message). The core hands them to the host; Mac and PC fit each stream inside both the host's stream-size setting and the viewer's limits, and cap the refresh rate.
- [x] Unknown physical size: a monitor with 0 mm (tvOS gives no size; Android TV reports unreliable DPI) gets a size the host can use. Mac: `CGVirtualDisplay` needs `sizeInMillimeters`; PC: check what the virtual display driver and Windows do with it.
- [x] Desktop scale for a TV: the viewer's `scale_percent` (200 for a 4K TV) gives a readable desktop on both hosts. Mac already picks HiDPI from it; PC: investigate setting the virtual monitor's DPI scale per viewer, and present the options to the user before building.
- [x] 4K60: bitrate table and H.264 level reach 3840×2160 at 60 Hz on both hosts (VideoToolbox auto level, Media Foundation level), within the 80 Mbit/s ceiling.
- [x] Developer tools: the Mac viewer can offer a simulated TV (3840×2160 or 1920×1080, 0 mm, scale 200, decoder limits like a TV chip), so Mac → "TV" and PC → "TV" are tested without a TV.
- [x] Tests: core tests for the capability limits; Mac host tests for the size fallback and scale; TESTING.md cases for the simulated TV from a Mac and a PC host.
- [x] Docs: PROTOCOL (what hosts do with Capabilities and 0 mm), ARCHITECTURE, DECISIONS.

## Acceptance Criteria
- [ ] Mac → simulated 4K TV and PC → simulated 4K TV: a 3840×2160 virtual display at a readable scale, the stream inside the simulated decoder limits.
- [ ] Existing viewers (Mac, PC, iPad, Android) behave as before.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-01 — User decision: three TV phases — hosts first (testable without a TV), then Apple TV, then Android TV. The user has no Apple TV and never will; the Android TV (4K) is in Eskişehir and is tested when the user is there.
