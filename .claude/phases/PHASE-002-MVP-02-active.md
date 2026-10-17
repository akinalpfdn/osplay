# Phase 002 (MVP-02) — macOS virtual displays (go/no-go gate)
Status: ACTIVE

DEVPLAN reference: Phase 1. Depends on: Phase 0.

## Goal
The Mac app can create and remove up to two real virtual displays on the M3 MacBook Air.

## Tasks
- [x] Private API declarations isolated in one Objective-C header and one Swift wrapper.
- [x] `VirtualDisplayProviding` protocol and its `CGVirtualDisplay` implementation: create with given pixel size, refresh rate, physical size, HiDPI flag and deterministic identity; update modes; destroy.
- [x] Temporary developer menu (debug builds only) to create/remove 1 or 2 test displays at chosen resolutions.

## Acceptance Criteria
- [ ] With the lid open on the M3 Air, two virtual displays appear simultaneously in System Settings → Displays.
  - Automated on the M3 Air (hardware test): both online at once next to the built-in screen. System Settings view: manual check pending.
- [ ] Windows can be dragged onto both; Mission Control treats them as separate displays.
- [ ] HiDPI "looks like" modes are selectable.
  - Automated: crisp 2× mode listed and activated through the API. System Settings picker: manual check pending.
- [ ] Removing and recreating with the same identity restores the previous arrangement.
  - Automated: same identity returns to the same origin. After a manual rearrangement: manual check pending.
- [ ] Destroying the provider removes the displays and windows move back to the built-in screen.
  - Automated: displays go offline after destroyAll. Window migration: manual check pending.
- [x] **If two simultaneous displays cannot be created, stop and document findings before any further phase.**

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- GO: two simultaneous virtual displays work on the M3 Air with the lid open (2026-10-17, macOS 26.6).
- Default resolution: activate the crisp HiDPI mode (looks like ½, backing = remote pixels); macOS alone picks a 4× backing.
- Mode selection (`activate`) added to the handle: needed for that default, not listed in DEVPLAN Phase 1.
- Hardware tests in a separate OsplayHardware scheme, not CI.
- Vendor ID 0x3E70 ("OSP" EDID packing, unregistered); serial = FNV-1a(peerID ‖ monitorIndex), never 0.
