# Phase 019 (SYM-03) — Several displays at once
Status: PENDING

DEVPLAN reference: Phase 13b (part YM-03-). Depends on: Phase 13. Runs before PHASE-015 (DEVPLAN Phase 14) — user decision 2026-10-21.

## Goal
A host can extend itself onto several computers at once (e.g. a laptop onto two other laptops), on macOS and Windows.

## Tasks
- [ ] Host apps listen again as soon as a viewer is accepted, so several sessions run at once; menus list every connected display device.
- [ ] Mac host: virtual displays owned per session; arrangement per device as today.
- [ ] Windows host: adding or removing a viewer's monitors while others stream (device restart with the new count, captures recover, modes reapplied).
- [ ] Role swap disabled while more than one viewer is connected; per-session Disconnect.
- [ ] TESTING.md scenarios for two viewers on each host platform.

## Acceptance Criteria
- [ ] A laptop extends onto two other computers at once, on macOS and on Windows.
- [ ] One viewer leaving does not interrupt the other.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
