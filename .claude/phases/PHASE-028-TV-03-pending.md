# Phase 028 (TV-03) — Apple TV viewer
Status: PENDING

DEVPLAN reference: Phase 16 (part TV-03). Depends on: PHASE-027 (TV-02).

## Goal
An Apple TV shows a Mac's or a PC's extra display over the network, from a tvOS app that is a viewer only, verified in the simulator and shipped as an experimental TestFlight build.

## Tasks
- [ ] Minimum tvOS version (ask the user); `Package.swift` builds the core and CoreBridge for tvOS; core tests on the tvOS simulator.
- [ ] `OsplayTV` XcodeGen target on `macos/Shared` (discovery, pairing, key store, decoding, presentation), like the iPad app.
- [ ] Remote-driven UI: nearby and paired computers, the pairing code shown on the TV, full-screen viewer; the Menu button disconnects; focus engine and the TV's safe area.
- [ ] Offers one display at the TV's output mode with 0 mm and a TV scale (TV-01); decoder limits from VideoToolbox.
- [ ] Lifecycle: the screen saver stays off while viewing; leaving the app ends the session and the host removes its virtual display.
- [ ] Local Network permission and the Bonjour service type; every string in English and Turkish; visual values from the theme.
- [ ] CI builds and tests the tvOS target.
- [ ] Distribution: tvOS added to the App Store Connect record, TestFlight build marked experimental (the user has no Apple TV).
- [ ] Docs: README (Apple TV, TestFlight), BUILDING, TESTING, ARCHITECTURE.

## Acceptance Criteria
- [ ] In the tvOS simulator: Mac → Apple TV and PC → Apple TV show one display; pairing, revoking and reconnecting work; leaving the app leaves no virtual display behind.
- [ ] The TestFlight build installs (verified by App Store Connect processing; no hardware test).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
