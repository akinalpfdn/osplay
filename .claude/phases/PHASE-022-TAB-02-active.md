# Phase 022 (TAB-02) — Android tablet viewer over Wi-Fi
Status: ACTIVE

DEVPLAN reference: Phase 15 (part TAB-02). Depends on: PHASE-021 (TAB-01).

## Goal
An Android tablet shows a Mac's or a PC's extra display over Wi-Fi, from an Android app that is a viewer only.

## Tasks
- [x] Google Play with application ID `com.akinalpfdn.osplay` (the user has a Play developer account); minimum Android 12 (API 31); test device: the user's Galaxy S23 Ultra phone (a Galaxy Tab A7 if found). Phones work as displays too.
- [x] Build the C++ core with the NDK (arm64-v8a, x86_64 for the emulator) and a JNI bridge.
- [x] Discovery with Android's NSD (service type and TXT records as on the other platforms).
- [x] H.264 decode with MediaCodec into a full-screen surface; statistics overlay.
- [x] Kotlin app: nearby computers, pairing code, paired devices, full-screen viewer, hide and disconnect; keys stored in the Android Keystore.
- [x] Lifecycle as on the iPad: screen kept on while viewing, background ends the session cleanly.
- [x] Every string in English and Turkish; visual values from the theme.
- [ ] CI builds and tests the Android app; a signed release bundle (AAB) for Google Play.
- [ ] Docs: README, BUILDING, TESTING, ARCHITECTURE.

## Acceptance Criteria
- [ ] Mac → Android tablet and PC → Android tablet show one display at the tablet's resolution over Wi-Fi. (2026-10-27: Mac → Galaxy S23 Ultra works, run from Android Studio; PC → Android open.)
- [ ] Pairing, revoking and reconnecting work as between computers.
- [ ] The app installs from Google Play (internal testing track at least).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-27 — User decision: Google Play, application ID `com.akinalpfdn.osplay`, minimum Android 12; test on the Galaxy S23 Ultra; phones are supported as displays too.
