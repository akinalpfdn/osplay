# Phase 004 (MVP-04) — Protocol codec and fixtures
Status: ACTIVE

DEVPLAN reference: Phase 3. Depends on: Phase 0.

## Goal
The complete wire format exists as a pure, tested library and a written specification.

## Tasks
- [x] `docs/PROTOCOL.md` documenting every message and packet field by field.
- [x] `core/protocol` encode/decode for all control messages and media packets with strict bounds checking.
- [x] Binary fixtures in `protocol/fixtures/` (valid and malformed cases).
- [x] C++ tests and Swift tests (via `CoreBridge`) that both run against the same fixtures.
- [x] Initial `capi` surface for version info and error codes.

## Acceptance Criteria
- [x] Round-trip tests pass for every message type. (fixtures + boundary round trips, all 19+4 types)
- [ ] Malformed fixtures are rejected with specific error codes, never crash; a fuzz target exists for the decoder.
- [ ] Swift and C++ suites pass on CI against identical fixtures.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Fixtures come from an independent Python encoder (protocol/tools/generate_fixtures.py); C++ and Swift must agree with it byte for byte; CI checks the files are current.
- CoreBridge moved into the Swift package (product `CoreBridge`); app depends on it.
- Added MediaSetup (0x13): UDP port + session token, host → viewer.
- Hello carries no major version: the 3-byte prefix (magic + version) is fixed forever, so a version mismatch is detected on the first byte-level read of any message.
- Media header (12 bytes: magic, version, type, u64 sequence) stays in clear as AEAD associated data; sequence becomes the nonce in Phase 4.
- Every non-last video/cursor fragment is full-size, so receivers can place fragments without knowing the total size.
- Fuzzing: libFuzzer job on Linux CI (Apple Clang lacks libFuzzer); every ctest run replays the fixtures through the same entry point.
- core has its own Expected/Status (osplay::); windows/shared keeps osplay::win's copy. Unify later in a separate refactor.
