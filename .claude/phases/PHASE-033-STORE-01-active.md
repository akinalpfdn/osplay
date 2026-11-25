# Phase 033 (STORE-01) — iPad and Apple TV apps ready for the App Store
Status: ACTIVE

DEVPLAN reference: after Phase 17. User decision 2026-11-26: only iPad + Apple TV go to the App Store; the Mac app stays on GitHub (DECISIONS.md).

## Goal
The iPad and Apple TV apps meet App Review's rules and tell users where to get Osplay for their Mac or PC.

## Tasks
- [ ] Settings: "Osplay for Mac and Windows" — a link on iPad, a QR code and short address on Apple TV (which cannot open links); the target is the README's install section.
- [ ] No other mobile platform named in the iPad/TV apps (guideline 2.3.10); no private API in the iPad/TV targets.
- [ ] `PRIVACY.md` (no data collected, everything local) for the privacy policy URL.
- [ ] Version 0.3.0 and a new build number for the iPad and Apple TV targets.
- [ ] Tests, TESTING.md case, docs.

## Acceptance Criteria
- [ ] The link opens the install section on iPad; the QR code on Apple TV (simulator) scans to the same page.
- [ ] iPad and Apple TV archives build for release.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
