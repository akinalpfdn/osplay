# Phase 032 (AUD-04) — Viewers play the sound
Status: DONE

DEVPLAN reference: Phase 17 (part AUD-04). Depends on: PHASE-029 (AUD-01).

## Goal
Every viewer plays the host's sound with low latency: Android phones, tablets and TVs, iPad, Apple TV, Mac and PC.

## Tasks
- [x] Android: AAudio (low-latency output) fed from the core's jitter buffer; JNI.
- [x] iPad, Apple TV and Mac: AVAudioEngine source node fed from CoreBridge.
- [x] PC viewer: WASAPI shared-mode render.
- [x] Viewers announce that they play audio; a mute switch in each viewer's controls.
- [x] Ship the Opus license in the Android app (THIRD_PARTY.md); the Windows installer already stages it.
- [x] Tests, TESTING.md cases, docs (ARCHITECTURE, DECISIONS; the README describes sound with the release).

## Acceptance Criteria
- [x] Mac → every viewer: heard on the PC, the iPad and the S23 (2026-11-23); Apple TV and Android TV share that code and were not heard on hardware. PC → every viewer is checked with AUD-03.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-21 — User decision: AUD-04 comes before AUD-03, starting with the PC viewer, so Mac → PC can be heard on a real monitor first. AUD-03 (PC host) follows.
- 2026-11-23 — First Mac → PC listening test: sound plays, two gaps in a few minutes, about 90–100 ms behind. User decision: least reserve 30 ms (20 ms concealed every reordered packet), WASAPI buffer 10 ms; the PC's stats overlay shows a sound line (DECISIONS.md).
- 2026-11-21 — PC viewer first without a shell change: the engine always announces sound, the Mac's button decides; a PC-side switch comes with the other viewers' controls.
- 2026-11-23 — User decision: Apple viewers (Mac, iPad, Apple TV) next, then Android. One shared `AVAudioEngine` output for all three; the overlay shows a sound line like the PC.
- 2026-11-23 — User decision: each viewer gets both a live mute in its viewing controls (the sound keeps coming and is read, the device plays silence) and a setting "Play the computer's sound" (off: the viewer does not announce sound, so the host offers no button).
- 2026-11-23 — User decision: the sharing Mac's volume keys and mute change the sound sent to a display too, like HDMI; done in AUD-04 for the Mac, the PC host follows in AUD-03.
- 2026-11-23 — Phone test on Wi-Fi: one bad patch took the reserve to 210 ms with 15 gaps and 58 concealed frames, then the review dropped it to 30 ms in one step. Proposed fix (grow by the measured gap, shrink at most 20 ms per review) — user decision: leave it for now and keep observing.
- 2026-11-23 — Closed: user confirmed the phone tests (mute, Mac volume keys, "Play the computer's sound" off). PR into dev, stacked on #48 (AUD-02).
