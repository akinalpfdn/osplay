# Phase 027 (TV-02) — Android TV viewer
Status: ACTIVE

DEVPLAN reference: Phase 16 (part TV-02). Depends on: PHASE-026 (TV-01).

## Goal
An Android TV or Google TV shows a Mac's or a PC's extra display over the network from the existing Android app.

## Tasks
- [x] Minimum Android version: stays Android 12 (minSdk 31), user decision 2026-11-11.
- [ ] TV support in the same APK: leanback launcher entry and banner, touchscreen not required, D-pad focus on every screen, the TV's safe area.
- [ ] Offers one display at the TV's mode with 0 mm and a TV scale (TV-01); decoder limits from MediaCodec (many TV chips decode H.264 4K only at 30 Hz).
- [ ] Lifecycle: the screen stays on while viewing; leaving the app ends the session.
- [ ] Every string in English and Turkish; visual values from the theme.
- [ ] Tests: view model and focus tests; TESTING.md cases for Android TV.
- [ ] Docs: README, TESTING, ARCHITECTURE, DECISIONS.

## Acceptance Criteria
- [ ] On the user's Vestel Android TV (Android 14): Mac → TV and PC → TV over Wi-Fi, at a readable scale and a smooth frame rate.
- [ ] From TV-01: the host makes a display in the TV's mode at a TV scale (crisp HiDPI on a Mac, 200% on a PC), and the stream stays within what the TV's decoder reports.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-11 — User decision: Android TV support keeps the app's minimum, Android 12 (minSdk 31). The user's test TV, a Vestel, runs Android 14. TVs sold before Android TV 12 (late 2022) that never got it are left out; lowering it can come later if users ask.
