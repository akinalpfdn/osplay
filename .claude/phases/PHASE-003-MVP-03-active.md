# Phase 003 (MVP-03) — Windows virtual display through VDD and the helper service
Status: ACTIVE

DEVPLAN reference: Phase 2. Depends on: Phase 0.

## Goal
The Windows engine can create and remove one virtual monitor with an exact custom resolution, without admin prompts during normal use.

## Tasks
- [x] Written findings in `docs/ARCHITECTURE.md` on VDD's control surface (config schema used, pipe commands, what needs a device restart).
- [x] `OsplayHelper` service: install/uninstall via command-line flags, named pipe with restrictive ACL, validated command set (configure, enable, disable, status).
- [x] Engine `IVirtualDisplayProvider` implemented as a helper client.
- [x] Temporary developer console tool to drive it.

## Acceptance Criteria
- [ ] With VDD installed, the tool creates one monitor at 2560×1664 at 60 Hz, visible in Display Settings.
- [ ] The monitor disappears entirely when the session ends; no ghost monitor remains after reboot.
- [ ] A non-admin user process can drive the helper; another user's process cannot connect to the pipe.
- [ ] Invalid commands and out-of-range values are rejected and logged.
  - Automated: protocol and request handler unit tests (rejection + log line). Live helper log check: pending on the PC.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- VDD reads settings only at device start; count 0 → 1; its pipe is open to everyone and is not used. Osplay writes the XML and enables/disables the device (DECISIONS: Windows virtual monitor control).
- Monitor lifetime = helper pipe client connection; service start/stop and console switch also remove it.
- Protocol, handler, XML renderer and provider logic are portable and tested on macOS (`portable` preset) and Windows.
- No monitor-count field in the helper protocol: MVP Windows host has one monitor; protocol is versioned.
- clang-tidy now runs on Windows code in the Windows CI job (MSVC compile commands).
- Manual verification on the developer PC also covers the deferred Phase 0 check (BUILDING.md Windows section on a fresh clone).
