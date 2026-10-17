# Phase 005 (MVP-05) — Crypto and handshake
Status: PENDING

DEVPLAN reference: Phase 4. Depends on: Phase 3.

## Goal
Two `core` instances can mutually authenticate and derive transport keys.

## Tasks
- [ ] Monocypher vendored with license.
- [ ] Noise XX implementation, transport cipher states, replay window, pairing code derivation from the handshake hash.
- [ ] Tests against published Noise test vectors.
- [ ] `capi` functions for identity key generation and import/export (storage stays in platform code).

## Acceptance Criteria
- [ ] Published Noise XX vectors pass.
- [ ] Tampered, replayed and reordered-beyond-window messages are rejected.
- [ ] Both sides derive the same 6-digit pairing code; any change to the transcript changes it.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
