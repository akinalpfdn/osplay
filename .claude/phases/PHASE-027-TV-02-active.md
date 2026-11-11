# Phase 027 (TV-02) — Android TV viewer
Status: ACTIVE

DEVPLAN reference: Phase 16 (part TV-02). Depends on: PHASE-026 (TV-01).

## Goal
An Android TV or Google TV shows a Mac's or a PC's extra display over the network from the existing Android app.

## Tasks
- [x] Minimum Android version: stays Android 12 (minSdk 31), user decision 2026-11-11.
- [x] TV support in the same APK: leanback launcher entry and banner, touchscreen not required, D-pad focus on every screen, the TV's safe area.
- [x] Offers one display at the TV's mode with 0 mm and a TV scale (TV-01); decoder limits from MediaCodec (many TV chips decode H.264 4K only at 30 Hz).
- [ ] Lifecycle: the screen stays on while viewing; leaving the app ends the session.
- [x] Every string in English and Turkish; visual values from the theme.
- [x] Tests: monitor description and decoder-limit tests (TelevisionTest); TESTING.md TV1–TV6. Focus behaviour is checked on the TV (TV5), not by a UI test.
- [ ] Docs: README, TESTING, ARCHITECTURE, DECISIONS.

## Acceptance Criteria
- [ ] On the user's Vestel Android TV (Android 14): Mac → TV and PC → TV over Wi-Fi, at a readable scale and a smooth frame rate.
- [ ] From TV-01: the host makes a display in the TV's mode at a TV scale (crisp HiDPI on a Mac, 200% on a PC), and the stream stays within what the TV's decoder reports.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-11 — User decision: Android TV support keeps the app's minimum, Android 12 (minSdk 31). The user's test TV, a Vestel, runs Android 14. TVs sold before Android TV 12 (late 2022) that never got it are left out; lowering it can come later if users ask.
- 2026-11-11 — A TV's Capabilities name the largest size its decoder plays at the mode's rate, then at 30 Hz: one size and rate is all Capabilities hold, and a 4K30 limit would also hold the default 1080p stream to 30 fps. Phones and tablets keep generous limits, since some phone decoders report odd panel sizes as unsupported although they play them.
