# Osplay wire protocol

> Status: not yet specified. Phase 3 replaces this page with the authoritative, field-by-field
> specification and the binary fixtures in `protocol/fixtures/`. Until then the fixed decisions
> are in [`DEVPLAN.md`](../DEVPLAN.md) → Protocol Requirements.

## Fixed decisions

- All integers little-endian. Every message and packet starts with a 2-byte magic and a 1-byte
  protocol version.
- Discovery: Bonjour / DNS-SD service type `_osplay._tcp`.
- Control channel: TCP, 4-byte length prefix, encrypted after the Noise XX handshake.
- Media channel: UDP, datagrams of at most 1200 bytes, one fragment of an H.264 Annex B access unit
  per packet, AEAD-encrypted.
- Hand-written fixed binary layouts; no protobuf or JSON on the wire.
