# Phase 019 (SYM-03) — Several displays at once
Status: ACTIVE

DEVPLAN reference: Phase 13b (part SYM-03). Depends on: Phase 13. Runs before PHASE-015 (DEVPLAN Phase 14) — user decision 2026-10-21.

## Goal
A host can extend itself onto several computers at once (e.g. a laptop onto two other laptops), on macOS and Windows.

## Tasks
- [ ] Host apps listen again as soon as a viewer is accepted, so several sessions run at once; menus list every connected display device.
- [ ] Mac host: virtual displays owned per session; arrangement per device as today.
- [ ] Windows host: adding or removing a viewer's monitors while others stream (each monitor has its own driver device, DECISIONS.md 2026-10-25: the other viewers' devices keep running, captures recover, modes reapplied).
- [ ] Role swap disabled while more than one viewer is connected; per-session Disconnect.
- [ ] TESTING.md scenarios for two viewers on each host platform.

## Acceptance Criteria
- [ ] A laptop extends onto two other computers at once, on macOS and on Windows.
- [ ] One viewer leaving does not interrupt the other.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-25 — User decision: the remaining STR-01 and SYM-02 tests (D8, D1, D5, D6, D7) come before this phase starts.
- 2026-10-25 — User decision: one row per connected computer (name, how many displays, its own Disconnect icon), then a "waiting for another computer" row; same layout on the Mac menu and the Windows panel.
- 2026-10-25 — User decision: a host opens at most 4 virtual displays across all viewers (a Mac still gives one viewer at most 2); a new viewer gets what is left and is refused as busy when nothing is.

## Known issues
- Seen once on 2026-10-25, not reproduced: after both viewers of a Mac host disconnected, the PC's next connection never reached the host (no handshake in the Mac log) while the second Mac reconnected. The Mac now logs the port every listener and advertisement uses; check those lines and the PC log if it happens again.
