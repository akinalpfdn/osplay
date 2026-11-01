# Phase 028 (TV-03) — Android TV viewer
Status: PENDING

DEVPLAN reference: Phase 16 (part TV-03). Depends on: PHASE-026 (TV-01).

## Goal
An Android TV or Google TV shows a Mac's or a PC's extra display over the network from the existing Android app.

## Tasks
- [ ] Minimum Android version: the app is Android 12+ (minSdk 31) while most TVs run Android 9–11. List what lowering it costs (NSD, binding to a Wi-Fi network without internet, the USB accessory, low-latency decoding) and let the user decide before building.
- [ ] TV support in the same APK: leanback launcher entry and banner, touchscreen not required, D-pad focus on every screen, the TV's safe area.
- [ ] Offers one display at the TV's mode with 0 mm and a TV scale (TV-01); decoder limits from MediaCodec (many TV chips decode H.264 4K only at 30 Hz).
- [ ] Lifecycle: the screen stays on while viewing; leaving the app ends the session.
- [ ] Every string in English and Turkish; visual values from the theme.
- [ ] Tests: view model and focus tests; TESTING.md cases for Android TV.
- [ ] Docs: README, TESTING, ARCHITECTURE, DECISIONS.

## Acceptance Criteria
- [ ] On the user's 4K Android TV (in Eskişehir): Mac → TV and PC → TV over Wi-Fi, at a readable scale and a smooth frame rate.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
