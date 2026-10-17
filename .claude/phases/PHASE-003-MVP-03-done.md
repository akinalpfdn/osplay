# Phase 003 (MVP-03) — Windows virtual display through VDD and the helper service
Status: DONE

DEVPLAN reference: Phase 2. Depends on: Phase 0.

## Goal
The Windows engine can create and remove one virtual monitor with an exact custom resolution, without admin prompts during normal use.

## Tasks
- [x] Written findings in `docs/ARCHITECTURE.md` on VDD's control surface (config schema used, pipe commands, what needs a device restart).
- [x] `OsplayHelper` service: install/uninstall via command-line flags, named pipe with restrictive ACL, validated command set (configure, enable, disable, status).
- [x] Engine `IVirtualDisplayProvider` implemented as a helper client.
- [x] Temporary developer console tool to drive it.

## Acceptance Criteria
- [x] With VDD installed, the tool creates one monitor at 2560×1664 at 60 Hz, visible in Display Settings. (PC test 2)
- [x] The monitor disappears entirely when the session ends; no ghost monitor remains after reboot. (PC test 1: Enter, X-close, kill, reboot)
- [x] A non-admin user process can drive the helper; another user's process cannot connect to the pipe.
  - Non-admin: verified on the PC. Other user: closed by design (user decision 2026-10-17, no second account) — DACL admits only SYSTEM and the console user's SID, and the helper re-checks client session and SID after connecting; an unidentifiable (anonymous) client was refused in PC test 1.
- [x] Invalid commands and out-of-range values are rejected and logged.
  - Unit tests plus PC test 1 (`Rejected request: truncated` in helper.log).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- VDD reads settings only at device start; count 0 → 1; its pipe is open to everyone and is not used. Osplay writes the XML and enables/disables the device (DECISIONS: Windows virtual monitor control).
- Monitor lifetime = helper pipe client connection; service start/stop and console switch also remove it.
- Protocol, handler, XML renderer and provider logic are portable and tested on macOS (`portable` preset) and Windows.
- No monitor-count field in the helper protocol: MVP Windows host has one monitor; protocol is versioned.
- clang-tidy now runs on Windows code in the Windows CI job (MSVC compile commands).
- Manual verification on the developer PC also covers the deferred Phase 0 check (BUILDING.md Windows section on a fresh clone).
- PC test 1 (2026-10-17): monitor add/remove, X-close, kill and reboot behave; 40/40 native tests pass on the PC. Found: mode stayed 800×600 (no mode change from user session) → GdiDisplayModeApplier in the engine; winget does not install the driver → BUILDING.md fixed; refusal log named the wrong SID → reason + client SID; transient SetupAPI error 13 after driver install → 3 attempts; status logged a removal without a monitor → fixed; service stop reason now logged (boot-time stop/start sequence to be checked in the next PC test). runas test not possible (no second account).
- PC test 2 (2026-10-17): mode 2560×1664 @ 60 applied, no spurious removal log, 43/43 tests. Found: a second client while a session is active got "helper not running" → new HELPER_BUSY result. Reboot test still to repeat with the stop-reason logging.
