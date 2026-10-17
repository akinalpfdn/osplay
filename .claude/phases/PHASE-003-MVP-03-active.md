# Phase 003 (MVP-03) — Windows virtual display through VDD and the helper service
Status: ACTIVE

DEVPLAN reference: Phase 2. Depends on: Phase 0.

## Goal
The Windows engine can create and remove one virtual monitor with an exact custom resolution, without admin prompts during normal use.

## Tasks
- [ ] Written findings in `docs/ARCHITECTURE.md` on VDD's control surface (config schema used, pipe commands, what needs a device restart).
- [ ] `OsplayHelper` service: install/uninstall via command-line flags, named pipe with restrictive ACL, validated command set (configure, enable, disable, status).
- [ ] Engine `IVirtualDisplayProvider` implemented as a helper client.
- [ ] Temporary developer console tool to drive it.

## Acceptance Criteria
- [ ] With VDD installed, the tool creates one monitor at 2560×1664 at 60 Hz, visible in Display Settings.
- [ ] The monitor disappears entirely when the session ends; no ghost monitor remains after reboot.
- [ ] A non-admin user process can drive the helper; another user's process cannot connect to the pipe.
- [ ] Invalid commands and out-of-range values are rejected and logged.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
