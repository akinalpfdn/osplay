# Phase 017 (SYM-01) — Windows host: every viewer monitor
Status: DONE (2026-10-22) — code complete; the acceptance tests below moved to PHASE-018 (SYM-02)

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
Moved to PHASE-018 (SYM-02), user decision 2026-10-22: the first one needs the Mac viewer to offer
every display, which is SYM-02's work, and SYM-02 tests all four directions anyway.
- [ ] → SYM-02: PC → Mac with an external monitor on the Mac: one Windows virtual display per Mac display.
- [ ] → SYM-02: PC → PC (laptop → two-monitor PC): two virtual displays on the laptop, one on each monitor.
- [ ] → SYM-02: Ending the session removes every virtual monitor; a crash leaves none behind (helper disables the device).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Helper protocol v2 (Configure with 1–4 modes); v1 refused, engine and helper ship together.
- Stopping one of several streams restarts the device with the others (the driver cannot drop one monitor).
- 2026-10-22 — User decision: close the phase with its code done and test it inside SYM-02.
