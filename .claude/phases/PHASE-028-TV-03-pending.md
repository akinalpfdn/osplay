# Phase 028 (TV-03) — Apple TV viewer
Status: PENDING

DEVPLAN reference: Phase 16 (part TV-03). Depends on: PHASE-027 (TV-02).

## Goal
An Apple TV shows a Mac's or a PC's extra display over the network, from a tvOS app that is a viewer only, verified in the simulator and shipped as an experimental TestFlight build.

## Tasks
- [x] Minimum tvOS version (ask the user); `Package.swift` builds the core and CoreBridge for tvOS; core tests on the tvOS simulator. (Done: tvOS 18 and the package build; the core tests run on macOS and the iPad simulator, not on tvOS.)
- [x] `OsplayTV` XcodeGen target on `macos/Shared` (discovery, pairing, key store, decoding, presentation), like the iPad app.
- [x] Remote-driven UI: nearby and paired computers, the pairing code shown on the TV, full-screen viewer; the Menu button disconnects; focus engine and the TV's safe area.
- [x] Offers one display at the TV's output mode with 0 mm and a TV scale (TV-01); decoder limits from VideoToolbox. (Done: mode, 0 mm, scale. Decoder limits stay generous: VideoToolbox has no size/rate query and every Apple TV 4K decodes H.264 4K60.)
- [ ] Lifecycle: the screen saver stays off while viewing; leaving the app ends the session and the host removes its virtual display.
- [x] Local Network permission and the Bonjour service type; every string in English and Turkish; visual values from the theme.
- [x] CI builds the tvOS target (its logic is the iPad's and tested there).
- [ ] Distribution: tvOS added to the App Store Connect record, TestFlight build marked experimental (the user has no Apple TV).
- [x] Docs: README (Apple TV, TestFlight), BUILDING, TESTING, ARCHITECTURE.

## Acceptance Criteria
- [ ] In the tvOS simulator: Mac → Apple TV and PC → Apple TV show one display; pairing, revoking and reconnecting work; leaving the app leaves no virtual display behind.
- [ ] The TestFlight build installs (verified by App Store Connect processing; no hardware test).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-20 — User decision: tvOS 18, as iPadOS 18 for the iPad. The Apple TV app is the iPad app's code built for tvOS (same bundle identifier, so one App Store record); only the screen description, the remote handling, two colours, the settings rows and the USB section differ. CI builds it; its logic is tested by the iPad tests. Mac → Apple TV checked in the tvOS 27.2 simulator by the user: works.
