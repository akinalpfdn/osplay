# Phase 018 (SYM-02) — Mac viewer: every display, all four directions
Status: ACTIVE

DEVPLAN reference: Phase 13b (part YM-02-). Depends on: Phase 13. Runs before PHASE-015 (DEVPLAN Phase 14) — user decision 2026-10-21.

## Goal
A Mac used as a display offers all its displays (with a picker like Windows'), and every direction — Mac→Mac, Mac→PC, PC→Mac, PC→PC — works with all monitors.

## Tasks
- [x] Mac viewer: offer every attached display (up to four), choose which in the menu (picture of the displays like the Windows panel).
- [x] Mac viewer: one full-screen presenter per stream on its display; hotkey and overlay per stream.
- [x] Display changes on the Mac viewer (plug, unplug, mode) handled per display.
- [x] TESTING.md: all four directions with every monitor (D1–D7).

## Acceptance Criteria
- [ ] Mac → Mac, Mac → PC, PC → Mac, PC → PC each show one display per offered monitor.
- [ ] Leaving a monitor out in the picker is respected on both platforms.
- [ ] From SYM-01: PC → Mac with an external monitor on the Mac gives one Windows virtual display per Mac display.
- [ ] From SYM-01: PC → PC (laptop → two-monitor PC) gives two virtual displays on the laptop, one on each monitor.
- [ ] From SYM-01: ending the session removes every Windows virtual monitor; a crash leaves none behind.
- [ ] Before PC-host tests: the PC's OsplayHelper service runs the current build (Settings shows "helper refused" while it is the pre-SYM-01 version).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Mac display identity across launches is name + place, like the Windows panel's (`LocalDisplay.key`); during a session displays are matched by `CGDirectDisplayID`.
- A display plugged in during a session is offered from the next connection, as on Windows.
- Unplugging the last viewed Mac display starts the session again with what is offered then (closing a MacBook's lid with an external display attached); Windows only stops that stream.
