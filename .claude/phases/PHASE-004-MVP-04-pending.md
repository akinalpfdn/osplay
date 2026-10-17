# Phase 004 (MVP-04) — Protocol codec and fixtures
Status: PENDING

DEVPLAN reference: Phase 3. Depends on: Phase 0.

## Goal
The complete wire format exists as a pure, tested library and a written specification.

## Tasks
- [ ] `docs/PROTOCOL.md` documenting every message and packet field by field.
- [ ] `core/protocol` encode/decode for all control messages and media packets with strict bounds checking.
- [ ] Binary fixtures in `protocol/fixtures/` (valid and malformed cases).
- [ ] C++ tests and Swift tests (via `CoreBridge`) that both run against the same fixtures.
- [ ] Initial `capi` surface for version info and error codes.

## Acceptance Criteria
- [ ] Round-trip tests pass for every message type.
- [ ] Malformed fixtures are rejected with specific error codes, never crash; a fuzz target exists for the decoder.
- [ ] Swift and C++ suites pass on CI against identical fixtures.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
