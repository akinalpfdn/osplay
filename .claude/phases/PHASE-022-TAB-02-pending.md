# Phase 022 (TAB-02) — Android tablet viewer over Wi-Fi
Status: PENDING

DEVPLAN reference: Phase 15 (part TAB-02). Depends on: PHASE-021 (TAB-01).

## Goal
An Android tablet shows a Mac's or a PC's extra display over Wi-Fi, from an Android app that is a viewer only.

## Tasks
- [ ] Ask the user which Android tablet(s) to test on, the minimum Android version, and Play Store or APK on GitHub Releases.
- [ ] Build the C++ core with the NDK (arm64-v8a, x86_64 for the emulator) and a JNI bridge.
- [ ] Discovery with Android's NSD (service type and TXT records as on the other platforms).
- [ ] H.264 decode with MediaCodec into a full-screen surface; statistics overlay.
- [ ] Kotlin app: nearby computers, pairing code, paired devices, full-screen viewer, hide and disconnect; keys stored in the Android Keystore.
- [ ] Lifecycle as on the iPad: screen kept on while viewing, background ends the session cleanly.
- [ ] Every string in English and Turkish; visual values from the theme.
- [ ] CI builds and tests the Android app; the release workflow attaches the APK (if APK distribution is chosen).
- [ ] Docs: README, BUILDING, TESTING, ARCHITECTURE.

## Acceptance Criteria
- [ ] Mac → Android tablet and PC → Android tablet show one display at the tablet's resolution over Wi-Fi.
- [ ] Pairing, revoking and reconnecting work as between computers.
- [ ] The app installs from the chosen channel.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
