# Security model

> The exact wire-level construction is in [`PROTOCOL.md`](PROTOCOL.md) → Encryption. Session
> handling (trust decisions, refusing unknown keys) lands in Phase 5 and pairing UI in Phases 10–12.

## Goals

- Only devices the user explicitly paired can start a stream.
- Screen content and control messages are confidential and tamper-evident on the LAN.
- A device on the same network cannot impersonate a paired device.
- The privileged Windows helper cannot be driven by other users or processes outside its
  validated command set.

## Summary of the design

- **Handshake:** Noise `XX_25519_ChaChaPoly_BLAKE2b`, implemented in `core` with Monocypher.
- **First pairing:** both devices show a 6-digit code derived from the handshake hash; the user
  confirms on both. Only then is the peer's static key stored as trusted.
- **Later connections:** the peer's static key must match a trusted entry.
- **Transport:** control (TCP) and media (UDP) are encrypted with keys from the Noise split,
  separate per direction and channel. UDP packets use a sliding replay window.
- **No unencrypted mode** exists in release builds.
- **Key storage:** macOS Keychain; Windows DPAPI-protected file. Core never stores keys and wipes
  secrets from memory when it is done with them.
- **Randomness:** core has no access to the operating system; the platform supplies its CSPRNG
  (`SecRandomCopyBytes`, `BCryptGenRandom`) through `osplay_random_source`.
- **Primitives:** Monocypher 4.0.3 (X25519, ChaCha20-Poly1305, BLAKE2b), vendored and unmodified.
- **Verification:** the handshake reproduces the published Noise XX test vectors byte for byte;
  every network parser, the handshake reader and the media decryptor are fuzzed in CI.

## Non-goals

- Protection against an attacker who already controls either machine.
- Hiding that two devices run Osplay (Bonjour advertisements are visible on the LAN).
