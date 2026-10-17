# Phase 005 (MVP-05) — Crypto and handshake
Status: ACTIVE

DEVPLAN reference: Phase 4. Depends on: Phase 3.

## Goal
Two `core` instances can mutually authenticate and derive transport keys.

## Tasks
- [x] Monocypher vendored with license.
- [x] Noise XX implementation, transport cipher states, replay window, pairing code derivation from the handshake hash.
- [x] Tests against published Noise test vectors.
- [x] `capi` functions for identity key generation and import/export (storage stays in platform code).

## Acceptance Criteria
- [x] Published Noise XX vectors pass. (cacophony + noise-c: all 3 handshake + 3 transport messages and the handshake hash, byte for byte)
- [x] Tampered, replayed and reordered-beyond-window messages are rejected. (control: tamper/replay/reorder; media: tampered header and body, replay, older than the 1024 window; forgeries do not move the window)
- [x] Both sides derive the same 6-digit pairing code; any change to the transcript changes it. (payload change, prologue change, bit flips; pinned value from an independent Python computation)

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Monocypher 4.0.3 vendored unmodified, built as a separate C target without Osplay's warning flags.
- AEAD via Monocypher's incremental API with a fresh context per message (first write = RFC 8439); covered by both Noise vectors.
- Randomness injected by the platform (RandomSource / osplay_random_source); deterministic in tests.
- Control keys = Noise Split; media keys = HKDF(ck, "Osplay media keys v1"); viewer = initiator.
- Pairing code = BLAKE2b-256(label || h) first 8 bytes LE mod 10^6.
- Media replay window 1024; sealer refuses repeated/decreasing sequence numbers (nonce reuse guard).
- core error enum moved to osplay::Error (protocol::Error is an alias); codes 11–16 appended.
- Fuzz entry point now also feeds the handshake reader and the media opener.
- CoreBridge: DeviceIdentity (generate via SecRandomCopyBytes, restore, device ID).
