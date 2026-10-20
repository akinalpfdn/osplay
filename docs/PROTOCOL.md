# Osplay wire protocol

Version 1.1. This document is authoritative: `core/src/protocol/` implements it, and
`protocol/tools/generate_fixtures.py` independently encodes the binary fixtures in
`protocol/fixtures/` from it. Both implementations must agree byte for byte.

## Conventions

- All integers are **little-endian**, unsigned unless written `i32`.
- `bytes[N]` is exactly N bytes; `string` is `u8 length` followed by that many bytes of UTF-8.
- Strings are at most **64 bytes**, valid UTF-8 (no overlong forms or surrogates) and contain no
  control characters (U+0000–U+001F, U+007F–U+009F).
- Fields marked *reserved* are written as 0 and must be 0 when read.
- Decoders are strict: a message must be exactly as long as its fields; trailing bytes, unknown
  enum values and out-of-range values are errors. An encoder refuses to produce anything a decoder
  would reject.
- Times are microseconds on the sender's monotonic clock (`*_us`).

## Layers

```
Discovery      Bonjour / DNS-SD  _osplay._tcp
Control (TCP)  frame = u32 length | payload
               payload = control message           (before the handshake completes)
               payload = AEAD(control message)     (after)
Media (UDP)    datagram = media header (12 bytes, in clear) | AEAD(media body)
```

The messages and packets below are defined in plaintext; "Encryption" specifies how they are
protected on the wire. The protocol fixtures are plaintext.

### Common prefix

Every control message and every media packet starts with the same three bytes:

| Offset | Field | Type | Value |
|-------:|-------|------|-------|
| 0 | magic | bytes[2] | `4F 50` ("OP") |
| 2 | version | u8 | protocol major version, **1** |

A receiver that reads a different version stops decoding (`UNSUPPORTED_VERSION`) and the session
ends with "update the other device". The prefix never changes across versions, so this check always
works.

## Discovery

Service type `_osplay._tcp`; the SRV record carries the TCP control port. TXT record keys:

| Key | Value |
|-----|-------|
| `pv` | protocol major version, decimal (`1`) |
| `name` | device name (UTF-8) |
| `id` | device ID: lowercase hex of the first 16 bytes of BLAKE2b-256 over the static public key |
| `roles` | `host`, `viewer` or `host,viewer` |
| `os` | `macos` or `windows` |

A host listens again on the same TCP port after every session (falling back to any free port when
it is taken). Bonjour browsing reports instances, not changed ports, so a viewer that resolved the
advertisement earlier would otherwise keep connecting to a closed port.

## Control channel (TCP)

Each frame is `u32 length` followed by `length` bytes of payload, 1 ≤ length ≤ 4096 for plaintext
messages (4112 with the 16-byte AEAD tag). A control message is:

| Offset | Field | Type |
|-------:|-------|------|
| 0 | magic | bytes[2] |
| 2 | version | u8 |
| 3 | type | u8 |
| 4 | body | type-specific, to the end of the payload |

| Type | Name | Direction | When |
|-----:|------|-----------|------|
| 0x01 | Hello | both | first message on a connection, plaintext |
| 0x02 | HandshakeMessage | both | Noise XX messages 1–3, plaintext |
| 0x03 | PairingConfirm | both | once per side after the handshake (see "Session flow") |
| 0x04 | Capabilities | both | after the handshake |
| 0x05 | ViewerMonitors | viewer → host | monitors the viewer lends |
| 0x06 | StreamConfig | host → viewer | one per stream |
| 0x07 | StreamStart | host → viewer | stream begins |
| 0x08 | StreamPause | both | viewer hidden / host asleep |
| 0x09 | StreamResume | both | |
| 0x0A | StreamStop | both | |
| 0x0B | KeyframeRequest | viewer → host | |
| 0x0C | Nack | viewer → host | missing fragments |
| 0x0D | ReceiverReport | viewer → host | periodic statistics |
| 0x0E | Ping | both | RTT and clock offset |
| 0x0F | Pong | both | reply to Ping |
| 0x10 | Heartbeat | both | keeps the session alive when idle |
| 0x11 | Error | both | before closing on an error |
| 0x12 | Bye | both | orderly close |
| 0x13 | MediaSetup | host → viewer | UDP port and session token |
| 0x14 | RoleSwap | both | reverse the roles of a running session (minor version 1) |

Stream IDs are 0–3. Monitor IDs are 0–255 and chosen by the viewer.

### 0x01 Hello

| Field | Type | Values |
|-------|------|--------|
| role | u8 | 1 host, 2 viewer |
| platform | u8 | 1 macOS, 2 Windows |
| protocol_minor | u8 | minor version of the sender (1) |
| reserved | u8 | 0 |
| device_name | string | 1–64 bytes |

### 0x02 HandshakeMessage

| Field | Type | Values |
|-------|------|--------|
| step | u8 | 1, 2 or 3 (Noise XX message number) |
| reserved | u8 | 0 |
| payload_length | u16 | 1–1024 |
| payload | bytes[payload_length] | Noise handshake message |

### 0x03 PairingConfirm

| Field | Type | Values |
|-------|------|--------|
| decision | u8 | 1 accepted, 2 rejected |

### 0x04 Capabilities

| Field | Type | Values |
|-------|------|--------|
| codecs | u32 | bit 0: H.264 Annex B. Other bits reserved. At least one bit set. |
| max_streams | u8 | 1–4 |
| flags | u8 | bit 0: sends a cursor stream; bit 1: renders a cursor stream. Other bits reserved. |
| max_width | u16 | 320–16384 |
| max_height | u16 | 240–16384 |
| max_refresh_hz | u16 | 24–1000 |

### 0x05 ViewerMonitors

| Field | Type | Values |
|-------|------|--------|
| count | u8 | 1–4 |
| monitors | Monitor × count | monitor IDs are unique |

Monitor:

| Field | Type | Values |
|-------|------|--------|
| monitor_id | u8 | 0–255 |
| width_px | u16 | 320–16384, physical pixels |
| height_px | u16 | 240–16384 |
| refresh_millihertz | u32 | 1000–500000 |
| physical_width_mm | u16 | 0–10000 (0 = unknown) |
| physical_height_mm | u16 | 0–10000 (0 = unknown) |
| scale_percent | u16 | 100–400 (backing scale × 100) |
| name | string | 0–64 bytes |

### 0x06 StreamConfig

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |
| monitor_id | u8 | a monitor from ViewerMonitors |
| width | u16 | 320–8192, even |
| height | u16 | 240–8192, even |
| refresh_millihertz | u32 | 1000–240000 |
| codec | u8 | 1 H.264 |
| profile | u8 | 66 Baseline, 77 Main, 100 High |
| cursor_mode | u8 | 0 cursor in the video, 1 separate cursor stream |
| reserved | u8 | 0 |
| bitrate_kbps | u32 | 100–1000000 |

### 0x07 StreamStart, 0x08 StreamPause, 0x09 StreamResume, 0x0A StreamStop

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |

### 0x0B KeyframeRequest

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |
| reason | u8 | 1 decoder error, 2 frame lost, 3 stream start, 4 stream resume |

### 0x0C Nack

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |
| frame_id | u32 | |
| range_count | u8 | 1–32 |
| ranges | Range × range_count | |

Range: `first_fragment u16`, `count u16`; count ≥ 1 and first_fragment + count ≤ 4096.

### 0x0D ReceiverReport

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |
| reserved | u8 | 0 |
| loss_permille | u16 | 0–1000, over the report interval |
| frames_received | u32 | cumulative |
| frames_dropped | u32 | cumulative |
| decode_time_us | u32 | mean over the report interval |
| jitter_us | u32 | inter-arrival jitter over the report interval |

### 0x0E Ping

| Field | Type |
|-------|------|
| ping_id | u32 |
| send_time_us | u64 |

### 0x0F Pong

| Field | Type | Meaning |
|-------|------|---------|
| ping_id | u32 | copied from the Ping |
| ping_send_time_us | u64 | copied from the Ping (requester's clock) |
| receive_time_us | u64 | when the Ping arrived (responder's clock) |
| reply_time_us | u64 | when the Pong was sent (responder's clock) |

### 0x10 Heartbeat

Empty body.

### 0x11 Error

| Field | Type | Values |
|-------|------|--------|
| code | u16 | see the table below; unknown codes are accepted and shown generically |

| Code | Meaning |
|-----:|---------|
| 1 | incompatible protocol version |
| 2 | pairing rejected by the user |
| 3 | peer is not trusted |
| 4 | device is busy with another session |
| 5 | malformed or unexpected message |
| 6 | stream configuration not supported |
| 7 | internal error |

### 0x12 Bye

| Field | Type | Values |
|-------|------|--------|
| reason | u8 | 1 user disconnected, 2 application quit, 3 going to sleep, 4 error, 5 role swap |

### 0x13 MediaSetup

| Field | Type | Values |
|-------|------|--------|
| udp_port | u16 | 1–65535, the host's media socket |
| session_token | bytes[16] | random per session |

### 0x14 RoleSwap

| Field | Type | Values |
|-------|------|--------|
| step | u8 | 1 request, 2 accept, 3 refuse |
| refusal | u8 | 0 for a request or an acceptance; when refusing: 1 cannot host (a permission, driver or service is missing), 2 busy |

See "Role swap" under "Session flow".

## Media channel (UDP)

A datagram is at most **1200 bytes**. It consists of a header that travels in clear (it is the
AEAD's associated data and its sequence number forms the nonce) followed by the body, which is
encrypted and followed by a 16-byte tag on the wire.

Media header (12 bytes):

| Offset | Field | Type | Values |
|-------:|-------|------|--------|
| 0 | magic | bytes[2] | `4F 50` |
| 2 | version | u8 | 1 |
| 3 | type | u8 | see below |
| 4 | sequence | u64 | per sender and direction, starts at 0, +1 per datagram |

| Type | Name | Direction |
|-----:|------|-----------|
| 0x01 | UdpHello | viewer → host |
| 0x02 | VideoFragment | host → viewer |
| 0x03 | CursorPosition | host → viewer |
| 0x04 | CursorShapeFragment | host → viewer |

### 0x01 UdpHello

| Field | Type |
|-------|------|
| session_token | bytes[16], from MediaSetup |

The host learns the viewer's UDP endpoint from the first valid UdpHello.

### 0x02 VideoFragment

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |
| flags | u8 | bit 0 keyframe (IDR); bit 1 codec configuration (SPS/PPS) present, only with bit 0. Other bits reserved. |
| frame_id | u32 | per stream, +1 per encoded frame |
| fragment_index | u16 | < fragment_count |
| fragment_count | u16 | 1–4096 |
| capture_time_us | u64 | host monotonic clock at capture (µs, keeps counting during sleep) |
| payload | bytes | 1–1154 bytes of the frame's H.264 Annex B access unit, to the end of the body |

Fragments of one frame carry consecutive slices of the access unit in order. Every fragment but the
last carries exactly 1154 bytes (12 header + 18 fields + 1154 payload + 16 tag = 1200). Every
keyframe access unit contains SPS and PPS in-band.

### 0x03 CursorPosition

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |
| visible | u8 | 0 or 1 |
| shape_id | u32 | the shape to draw |
| x | i32 | hotspot position in stream pixels; may lie outside the stream |
| y | i32 | |
| capture_time_us | u64 | |

Sent at most at the stream's refresh rate; the newest position wins.

### 0x04 CursorShapeFragment

| Field | Type | Values |
|-------|------|--------|
| stream_id | u8 | 0–3 |
| shape_id | u32 | changes whenever the shape changes |
| width | u16 | 1–256 |
| height | u16 | 1–256 |
| hotspot_x | u16 | < width |
| hotspot_y | u16 | < height |
| fragment_index | u16 | < fragment_count |
| fragment_count | u16 | = ceil(width × height × 4 / 1155) |
| payload | bytes | slice of the BGRA image (premultiplied alpha, rows top to bottom, no padding) |

Every fragment but the last carries exactly 1155 bytes; the last carries the remainder. A shape is
sent when it changes and again with every keyframe, so a viewer that joined late recovers. Shapes
are not retransmitted: a viewer that keeps receiving visible positions naming a shape it lacks for
100 ms sends KeyframeRequest (reason 2), which brings the shape again. A shape already delivered
is not delivered again.

## Session flow

1. The viewer connects to the host's control port; a host that does not accept within **5 s** (asleep,
   gone, an old advertisement) ends the attempt with TIMED_OUT. Both sides send **Hello**; a different
   major version ends the connection with Error 1.
2. **Noise XX** (viewer = initiator) in three HandshakeMessages. Everything after it is encrypted.
   The handshake must finish within 10 s.
3. **Pairing.** Each side checks the peer's static key against its own trust store:
   - trusted: it sends PairingConfirm(accepted) at once;
   - unknown: it shows the pairing code and sends PairingConfirm once the user decides;
   - blocked: it sends Error 3 and closes.

   Each side sends exactly one PairingConfirm. The session continues only when both accepted; a
   rejection ends it with Error 2. A first pairing stores the key on both sides.
4. **Negotiation.** Both send Capabilities; the viewer sends ViewerMonitors. The host answers with
   MediaSetup followed by one StreamConfig per stream.
5. **UDP association.** The viewer sends UdpHello to the MediaSetup port on the host's address
   every 250 ms until media flows, then once per second as a keepalive. The host sends media to the
   source of the latest valid UdpHello (so it follows a viewer whose address changes). On the first
   one it sends StreamStart for every stream and starts with a keyframe.
6. **Streaming.** Every second each side sends Ping (RTT and clock offset) and the viewer sends a
   ReceiverReport per stream. A side that has sent nothing for 1 s sends Heartbeat. A side that has
   received nothing for **10 s** closes with a timeout, so shorter network interruptions are survived
   without user action.
7. **Close.** Bye, then the TCP connection closes after the queued data.

### Role swap

Either side may ask to reverse the roles while streaming or paused, so the computer used as a
display becomes the host and the other one its display.

1. The asking side sends RoleSwap(request). It has at most one request outstanding; one without an
   answer after **10 s** is given up locally.
2. The other side checks that it can host and answers RoleSwap(accept) followed by **Bye(5)**, or
   RoleSwap(refuse, reason); a refusal leaves the session running. A request, acceptance or refusal
   that arrives when none is expected ends the session with Error 5.
3. If both sides ask at once, they agree: the host answers with accept and Bye(5), the viewer waits
   for it.
4. After an accepted swap the former viewer starts a host session and advertises it; the former host
   browses and connects to the advertisement whose `id` matches the peer's static key (Discovery).
   The keys are already trusted, so no pairing code is shown.

A peer with protocol minor 0 does not know RoleSwap and ends the session with Error 5 when it
arrives; Hello tells each side the other's minor version, so the request is not sent to such peers.

### Loss recovery

- The viewer delivers frames of a stream in frame_id order.
- Timing restarts whenever a frame makes progress: a new fragment arrives, or a later frame shows
  that it is missing entirely. So a large keyframe that is still arriving is not given up. A frame
  that makes no progress for half a frame interval is NACKed once; a frame of which nothing
  arrived is NACKed as the range (0, 4096).
- The host keeps the last 16 frames (at most 8 MiB) for retransmission.
- A loss is only visible when a later fragment arrives, so a stream whose last frame was lost
  entirely would never recover once the screen stops changing. The host therefore sends the last
  fragment of a stream's newest frame again 50 ms, 200 ms and 700 ms after that frame, unless a
  newer frame went out. A viewer that already has the frame ignores it; one that lost the frame
  learns of it and NACKs the rest. No new message is involved.
- Time during which the viewer's own I/O thread did not run (a gap of more than 8 ms between its
  timer ticks) is not counted towards these deadlines: after such a stall the rest of a frame may
  still be waiting in the socket.
- A frame without progress for a frame interval plus max(RTT, 2 ms) is dropped. The viewer then
  discards frames until the next keyframe and sends KeyframeRequest (reason 2), at most every
  250 ms per stream. More than 8 frames in flight has the same effect.
- The host turns requests closer together than 100 ms into a single keyframe.

### Bitrate adaptation

The host adapts each stream's encoder bitrate from the viewer's ReceiverReports (once per second).
An interval is congested when the viewer lost at least 1% of its frames or the host resent at least
5% of the fragments it sent; the target then drops to 75%. After three clean intervals in a row it
rises by 8%. It never exceeds the bitrate in StreamConfig and never drops below a quarter of it
(at least 1 Mbit/s). Changes under 5% are not passed to the encoder. Nothing on the wire changes:
StreamConfig keeps the configured bitrate.

## Encryption

### Handshake

Noise Protocol Framework revision 34, pattern **`Noise_XX_25519_ChaChaPoly_BLAKE2b`**, prologue
empty. The viewer connects and is the **initiator**; the host is the **responder**. The three Noise
messages travel as HandshakeMessage (0x02) payloads, steps 1–3, after both Hello messages. The
implementation is validated against the published XX vectors of cacophony and noise-c
(`core/tests/data/`).

- Static keys are the devices' identities. A DH result of all zeros (low-order peer key) aborts
  the handshake.
- The handshake payloads are empty in version 1.
- Any failure (authentication, order, size) ends the handshake; it is never resumed.

### Pairing code

Both devices show the same six-digit code after the handshake:

```
digest = BLAKE2b-256("Osplay pairing code v1" || handshake_hash)
code   = (first 8 bytes of digest as u64 little-endian) mod 1000000, shown with leading zeros
```

`handshake_hash` is the final Noise `h` (64 bytes). Any change to the transcript changes the code.

### Session keys

From the final chaining key `ck`, using Noise's HKDF (HMAC-BLAKE2b, two outputs; each truncated to
32 bytes):

| Keys | Derivation | Use |
|------|------------|-----|
| control i→r, control r→i | Noise `Split()`: HKDF(ck, empty) | control channel, as Noise transport messages |
| media i→r, media r→i | HKDF(ck, "Osplay media keys v1") | media channel |

First output: initiator to responder; second: responder to initiator. The four keys differ.

### Control channel after the handshake

Each frame payload is a Noise transport message: `ChaCha20-Poly1305(key, nonce, ad = empty,
control message) || tag`. The nonce is the CipherState counter (0, 1, 2, … per direction), encoded
as 32 zero bits followed by the 64-bit counter little-endian. TCP delivers in order, so a replayed,
reordered or altered frame fails authentication and ends the session.

### Media channel

```
datagram = media header (12 bytes, clear)
         | ChaCha20-Poly1305(media key of the sender's direction,
                             nonce = 32 zero bits || sequence (u64 LE),
                             ad = media header, plaintext = media body)
         | tag (16 bytes)
```

A sender never repeats a sequence number. A receiver keeps a sliding window of the last 1024
sequence numbers: datagrams older than the window or already seen are dropped (`REPLAYED`); others
may arrive in any order. The window only moves after a datagram authenticates.

## Error codes

Decoding and encoding report these codes, which are also the values of `osplay_result` in
`core/include/osplay/osplay.h` (values never change; new codes are appended):

| Code | Name | Meaning |
|-----:|------|---------|
| 0 | OK | |
| 1 | INVALID_ARGUMENT | null pointer or unknown enum passed to the API |
| 2 | BUFFER_TOO_SMALL | the output buffer cannot hold the result |
| 3 | TRUNCATED | input ends before a field |
| 4 | BAD_MAGIC | the first two bytes are not "OP" |
| 5 | UNSUPPORTED_VERSION | the version byte is not 1 |
| 6 | UNKNOWN_TYPE | unknown message or packet type |
| 7 | TRAILING_BYTES | input continues after the last field |
| 8 | VALUE_OUT_OF_RANGE | a field violates its documented range, a reserved field is not 0, or a size limit is exceeded |
| 9 | INVALID_STRING | a string is not valid UTF-8, contains control characters or is too long |
| 10 | TOO_LARGE | input is larger than the largest valid message or datagram |
| 11 | DECRYPTION_FAILED | authentication failed: wrong key, altered or forged data |
| 12 | HANDSHAKE_OUT_OF_ORDER | a handshake step was used out of order, or after a failure |
| 13 | REPLAYED | a media datagram was already received or is older than the replay window |
| 14 | NONCE_EXHAUSTED | a cipher reached its last nonce |
| 15 | INVALID_KEY | an all-zero secret key, or a peer key producing an all-zero shared secret |
| 16 | RANDOM_UNAVAILABLE | the platform's random source failed |
| 17 | TIMED_OUT | the handshake or the peer stopped responding (see "Session flow") |
| 18 | CONNECTION_CLOSED | the peer ended the session (Bye) or the connection closed |
| 19 | PEER_ERROR | the peer sent an Error message |
| 20 | UNEXPECTED_MESSAGE | a valid message arrived in a state where it is not allowed |
| 21 | NOT_TRUSTED | the peer's key is blocked locally |
| 22 | PAIRING_REJECTED | a user rejected the pairing code |
| 23 | PEER_BUSY | the peer is in another session |
| 24 | INVALID_STATE | the command is not valid in the session's current state |
| 25 | NETWORK | a socket could not be opened, connected or used |
| 26 | INTERNAL | the core could not allocate memory or start its I/O thread |
| 27 | PEER_SLEEPING | the peer closed the session with Bye 3 (going to sleep); it is expected back |

## Fixtures

`protocol/fixtures/manifest.tsv` lists every fixture: file, kind (`control` or `media`), expected
outcome (`valid` with the message type, or `error` with the error code) and a description.
`protocol/tools/generate_fixtures.py` writes the binary files; CI fails if they differ from what the
generator produces. The C++ and Swift test suites both read the manifest and must reach the same
outcome for every file; valid fixtures must also re-encode to identical bytes.
