# Phase 021 (TAB-01) — iPad viewer over Wi-Fi
Status: ACTIVE

DEVPLAN reference: Phase 15 (part TAB-01). Depends on: Phase 14.

## Goal
An iPad shows a Mac's or a PC's extra display over Wi-Fi, from an iPad app that is a viewer only.

## Tasks
- [x] Minimum iPadOS 18 (user decision 2026-10-26); a separate App Store app, since the Mac app cannot be in the Mac App Store (private CGVirtualDisplay API).
- [x] `Package.swift`: build the core and CoreBridge for iPadOS; core tests on the iOS simulator.
- [x] Move the Mac viewer pipeline (Bonjour browse, session, H.264 decode, presentation, statistics overlay) into a module shared by the macOS app and the iPad app; the Mac app behaves the same.
- [x] iPad app (XcodeGen target): nearby computers, pairing code, paired devices, full-screen viewer offering one display at the iPad's native resolution, hide and disconnect.
- [x] Lifecycle: the screen stays awake while viewing; going to the background or locking ends the session cleanly and the host removes its virtual display.
- [x] Local Network permission and the Bonjour service type in the iPad Info.plist.
- [x] Every string in English and Turkish; visual values from the theme.
- [ ] Hosts: Mac and Windows hosts accept an iPad viewer and create a virtual display at its size.
- [x] CI builds and tests the iPad target.
- [ ] Distribution: App Store Connect record, privacy answers, TestFlight build, App Store review.
- [ ] Docs: README (iPad download), BUILDING, TESTING (iPad cases), ARCHITECTURE.

## Acceptance Criteria
- [ ] Mac → iPad and PC → iPad show one display at the iPad's resolution over Wi-Fi. (2026-10-27: Mac → iPad works on the user's iPad A16, run from Xcode; PC → iPad open.)
- [ ] Pairing, revoking and reconnecting work as between computers.
- [ ] Sending the iPad app to the background ends the session without leaving a virtual display on the host.
- [ ] The app passes App Store review.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-26 — User decision: tablets are viewers only and forward no touch input.
- 2026-10-26 — User decision: minimum iPadOS 18. Test device: the user's base iPad (11th generation, A16, 2025).
- 2026-10-26 — Shared Apple code lives in `macos/Shared`, compiled into both apps (DECISIONS.md). Protocol 1.2 adds the iPadOS and Android platforms and the `pm` TXT key; iPads need Osplay 0.2 on the computer.
- 2026-10-26 — Rotating the iPad restarts the session with the new shape; the background ends it and the app reconnects on return.
