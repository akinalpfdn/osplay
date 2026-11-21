# Phase 032 (AUD-04) — Viewers play the sound
Status: ACTIVE

DEVPLAN reference: Phase 17 (part AUD-04). Depends on: PHASE-029 (AUD-01).

## Goal
Every viewer plays the host's sound with low latency: Android phones, tablets and TVs, iPad, Apple TV, Mac and PC.

## Tasks
- [ ] Android: AAudio (low-latency output) fed from the core's jitter buffer; JNI.
- [ ] iPad, Apple TV and Mac: AVAudioEngine source node fed from CoreBridge.
- [ ] PC viewer: WASAPI shared-mode render.
- [ ] Viewers announce that they play audio; a mute switch in each viewer's controls.
- [ ] Ship the Opus license in the Android app (THIRD_PARTY.md); the Windows installer already stages it.
- [ ] Tests, TESTING.md cases, docs (README, ARCHITECTURE, DECISIONS).

## Acceptance Criteria
- [ ] Mac → every viewer and PC → every viewer: sound in step with the picture, no crackles on a clean Wi-Fi link, recovers after a dropout.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-21 — User decision: AUD-04 comes before AUD-03, starting with the PC viewer, so Mac → PC can be heard on a real monitor first. AUD-03 (PC host) follows.

