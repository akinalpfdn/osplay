# Phase 030 (AUD-02) — Mac host: the Mac's sound on the display
Status: PENDING

DEVPLAN reference: Phase 17 (part AUD-02). Depends on: PHASE-029 (AUD-01).

## Goal
A Mac that extends onto a display can play its sound there instead of on its own speakers.

## Tasks
- [ ] Capture the system mix with a Core Audio process tap (macOS 14.2+), muted on the Mac while tapped (`mutedWhenTapped`); NSAudioCaptureUsageDescription and the permission in onboarding.
- [ ] A per-viewer choice in the menu: play sound on this display (one viewer at a time when several are connected; default off until chosen).
- [ ] Feed the core's audio API; stop and unmute when the session ends or the choice is turned off.
- [ ] macOS 14.0–14.1: no tap; the choice is hidden with a note.
- [ ] Tests (view model, tap lifecycle with a fake), TESTING.md cases, docs.

## Acceptance Criteria
- [ ] Mac → Android TV and Mac → PC: sound plays on the display, not on the Mac, in step with the picture; turning it off brings it back to the Mac.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
