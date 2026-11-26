# Phase 034 (STORE-02) — App Store listing and review
Status: ACTIVE

Depends on: PHASE-033.

## Goal
The iPad and Apple TV apps are submitted for review in one App Store record.

## Tasks
- [ ] App Store Connect: tvOS platform added to the record `com.akinalpfdn.osplay.ipad` (the user, in the browser).
- [x] Listing text in English and Turkish: name, subtitle, keywords, description, what's new, support and privacy URLs (docs/STORE-LISTING.md).
- [x] Screenshots: 13-inch iPad (2732×2048) and Apple TV (3840×2160), four each, captioned in English and Turkish (~/Desktop/Osplay App Store, not in the repo).
- [ ] Privacy labels ("Data Not Collected"), age rating, category, free price.
- [ ] Review notes: a Mac or PC running Osplay is needed; a short demo video link.
- [ ] Archive and upload the iPad and tvOS builds; submit both for review.

## Acceptance Criteria
- [ ] Both platforms are "Waiting for Review" with complete metadata.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-26 — The privacy policy URL points at PRIVACY.md on main: it must reach main (with this branch's PR and a dev → main merge, or straight to main like a README edit) before the submission.
- 2026-11-26 — Screenshots taken in Xcode 27's Device Hub (it replaces Simulator.app); the tvOS Settings and remote fixes found while taking them are in f1ff214 and before.
