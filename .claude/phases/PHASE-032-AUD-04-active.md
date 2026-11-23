# Phase 032 (AUD-04) — Viewers play the sound
Status: ACTIVE

DEVPLAN reference: Phase 17 (part AUD-04). Depends on: PHASE-029 (AUD-01).

## Goal
Every viewer plays the host's sound with low latency: Android phones, tablets and TVs, iPad, Apple TV, Mac and PC.

## Tasks
- [x] Android: AAudio (low-latency output) fed from the core's jitter buffer; JNI.
- [x] iPad, Apple TV and Mac: AVAudioEngine source node fed from CoreBridge.
- [x] PC viewer: WASAPI shared-mode render.
- [x] Viewers announce that they play audio; a mute switch in each viewer's controls.
- [x] Ship the Opus license in the Android app (THIRD_PARTY.md); the Windows installer already stages it.
- [ ] Tests, TESTING.md cases, docs (README, ARCHITECTURE, DECISIONS).

## Acceptance Criteria
- [ ] Mac → every viewer and PC → every viewer: sound in step with the picture, no crackles on a clean Wi-Fi link, recovers after a dropout.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-21 — User decision: AUD-04 comes before AUD-03, starting with the PC viewer, so Mac → PC can be heard on a real monitor first. AUD-03 (PC host) follows.
- 2026-11-23 — First Mac → PC listening test: sound plays, two gaps in a few minutes, about 90–100 ms behind. User decision: least reserve 30 ms (20 ms concealed every reordered packet), WASAPI buffer 10 ms; the PC's stats overlay shows a sound line (DECISIONS.md).
- 2026-11-21 — PC viewer first without a shell change: the engine always announces sound, the Mac's button decides; a PC-side switch comes with the other viewers' controls.
- 2026-11-23 — User decision: Apple viewers (Mac, iPad, Apple TV) next, then Android. One shared `AVAudioEngine` output for all three; the overlay shows a sound line like the PC.
- 2026-11-23 — User decision: each viewer gets both a live mute in its viewing controls (the sound keeps coming and is read, the device plays silence) and a setting "Play the computer's sound" (off: the viewer does not announce sound, so the host offers no button).
