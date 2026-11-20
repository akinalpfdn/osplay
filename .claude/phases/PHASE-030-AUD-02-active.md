# Phase 030 (AUD-02) — Mac host: the Mac's sound on the display
Status: ACTIVE

DEVPLAN reference: Phase 17 (part AUD-02). Depends on: PHASE-029 (AUD-01).

## Goal
A Mac that extends onto a display can play its sound there instead of on its own speakers.

## Tasks
- [x] Capture the system mix with a Core Audio process tap (macOS 14.2+), muted on the Mac while tapped (`mutedWhenTapped`); NSAudioCaptureUsageDescription and the permission in onboarding.
- [x] A per-viewer choice in the menu: play sound on this display (one viewer at a time when several are connected; default off until chosen).
- [x] Feed the core's audio API; stop and unmute when the session ends or the choice is turned off.
- [x] macOS 14.0–14.1: no tap; the choice is hidden with a note.
- [x] Ship the Opus license (`core/third_party/opus/COPYING`) in the Mac, iPad and Apple TV apps and show it with the other notices (THIRD_PARTY.md: binary redistributions must reproduce it; the core in every app links Opus since AUD-01).
- [x] Tests (view model, tap lifecycle with a fake), TESTING.md cases, docs.

## Acceptance Criteria
- [ ] On the Mac (automated tests and a check on the developer's Mac): with the choice on, the tap captures the system mix, the Mac goes silent and the host sends audio (stats show the audio bitrate); turning it off or ending the session brings the sound back to the Mac.
- [ ] Hardware, run together with AUD-04 (user decision 2026-11-20): Mac → Android TV and Mac → PC play the sound on the display, not on the Mac, in step with the picture.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-20 — User decision: AUD-02 closes on automated tests and a Mac-side check; the listening test on Android TV and PC runs after AUD-04, when the viewers play sound.
- 2026-11-20 — Global process tap muted while tapped; TCC's own functions read and request the permission, optional in onboarding and asked again from the sound button (DECISIONS.md).
- 2026-11-20 — The Mac-side check runs in the Host media test: its in-process viewer announces sound, reads it like a device and shows the level (TESTING.md S1–S5).

