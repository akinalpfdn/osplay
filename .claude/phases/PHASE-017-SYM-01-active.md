# Phase 017 (SYM-01) — Windows host: every viewer monitor
Status: ACTIVE

DEVPLAN reference: Phase 13b (part YM-01-). Depends on: Phase 13. Runs before PHASE-015 (DEVPLAN Phase 14) — user decision 2026-10-21.

## Goal
A Windows host makes one virtual display per monitor the viewer offers (up to four), like the Mac host.

## Tasks
- [x] Helper protocol v2: Configure takes a list of modes; vdd_settings.xml gets the monitor count and every mode.
- [x] Engine: create, find and size N virtual monitors (CCD paths of the driver's adapter), each with its own mode.
- [x] Host orchestrator and capture: one Desktop Duplication + encoder pipeline per monitor; max_streams up to 4.
- [x] Windows viewer offers up to four monitors (was two).
- [x] Tests (helper protocol, provider, orchestrator), docs (ARCHITECTURE, PROTOCOL if touched, BUILDING checks).

## Acceptance Criteria
- [ ] PC → Mac with an external monitor on the Mac: one Windows virtual display per Mac display.
- [ ] PC → PC (laptop → two-monitor PC): two virtual displays on the laptop, one on each monitor.
- [ ] Ending the session removes every virtual monitor; a crash leaves none behind (helper disables the device).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Helper protocol v2 (Configure with 1–4 modes); v1 refused, engine and helper ship together.
- Stopping one of several streams restarts the device with the others (the driver cannot drop one monitor).
