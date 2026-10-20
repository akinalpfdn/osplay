# Phase 016 (SWAP-01) — Instant role swap
Status: ACTIVE

DEVPLAN reference: Phase 12b. Depends on: Phase 12. Runs next, before PHASE-014 (DEVPLAN Phase 13)
— user decision 2026-10-20.

## Goal
One global shortcut on either computer reverses the roles of a running session: the computer used
as a display becomes the host and the other one becomes its display, without pairing again.

## Tasks
- [ ] Core: a role-swap control message (request, accept), protocol minor version bump, PROTOCOL.md, fixtures, fuzz corpus, tests.
- [ ] Core/session: on swap both sides close cleanly; the new viewer reconnects to the same peer by device ID.
- [ ] Mac: recordable swap shortcut (default ⌃⌥⌘S), swap in ConnectionViewModel (host ⇄ viewer), menu shows the swap state.
- [ ] Windows engine/shell: recordable swap shortcut (default Ctrl+Alt+Shift+S, registered while a session runs), swap in ConnectionViewModel.
- [ ] Both: swap refused cleanly when the other side cannot host (missing permission, helper/driver) with a localized reason.
- [ ] Docs: ARCHITECTURE, TROUBLESHOOTING, BUILDING test steps; DECISIONS entries.

## Acceptance Criteria
- [ ] Pressing the shortcut on either computer swaps the roles without a pairing code.
- [ ] Measured swap time (shortcut to first frame) is recorded on the reference hardware.
- [ ] A refused or failed swap leaves both sides in a clear state (no half-open session, message shown).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
