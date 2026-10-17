# Phase 002 (MVP-02) — macOS virtual displays (go/no-go gate)
Status: PENDING

DEVPLAN reference: Phase 1. Depends on: Phase 0.

## Goal
The Mac app can create and remove up to two real virtual displays on the M3 MacBook Air.

## Tasks
- [ ] Private API declarations isolated in one Objective-C header and one Swift wrapper.
- [ ] `VirtualDisplayProviding` protocol and its `CGVirtualDisplay` implementation: create with given pixel size, refresh rate, physical size, HiDPI flag and deterministic identity; update modes; destroy.
- [ ] Temporary developer menu (debug builds only) to create/remove 1 or 2 test displays at chosen resolutions.

## Acceptance Criteria
- [ ] With the lid open on the M3 Air, two virtual displays appear simultaneously in System Settings → Displays.
- [ ] Windows can be dragged onto both; Mission Control treats them as separate displays.
- [ ] HiDPI "looks like" modes are selectable.
- [ ] Removing and recreating with the same identity restores the previous arrangement.
- [ ] Destroying the provider removes the displays and windows move back to the built-in screen.
- [ ] **If two simultaneous displays cannot be created, stop and document findings before any further phase.**

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
