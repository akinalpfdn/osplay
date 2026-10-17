# Security model

> Status: outline. The handshake and key handling are specified and implemented in Phases 3–5;
> this page will then document them exactly. The fixed decisions are in
> [`DEVPLAN.md`](../DEVPLAN.md) → Protocol Requirements → Security model.

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
- **Key storage:** macOS Keychain; Windows DPAPI-protected file.

## Non-goals

- Protection against an attacker who already controls either machine.
- Hiding that two devices run Osplay (Bonjour advertisements are visible on the LAN).
