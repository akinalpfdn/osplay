# Phase 006 (MVP-06) — Transport and session
Status: ACTIVE

DEVPLAN reference: Phase 5. Depends on: Phase 4.

## Goal
Two `core` instances on the same machine or LAN can open an encrypted session and exchange control messages and media packets.

## Tasks
- [x] Asio TCP listener/connector for control, UDP media socket, UDP association via session token.
- [x] Packetizer and reassembler with deadlines, NACK bookkeeping, keyframe request rate limiting.
- [x] Session state machine, heartbeat, ping/pong with RTT and clock offset estimation, stats aggregation.
- [x] `capi` for creating host/viewer sessions, pushing access units (host), receiving access units (viewer), callbacks for state, stats and errors, and a log callback.
- [x] Loopback integration test that streams synthetic access units through a simulated lossy link.

## Acceptance Criteria
- [x] Loopback test reassembles 100% of frames on a clean link and recovers via NACK or IDR under 5% simulated loss.
- [x] Session survives a 3-second network interruption and resumes without user action.
- [x] No unencrypted bytes leave the process after the handshake (verified by a test that inspects the wire).
- [x] Thread sanitizer and address sanitizer runs are clean on macOS CI.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Sans-I/O `SessionEngine` under a thin `AsioSession` transport, one I/O thread per session.
- Each side sends exactly one PairingConfirm after the handshake, also for trusted peers.
- Asio 1.38.2 vendored as headers (`core/third_party/asio`), an INTERFACE target shared with tests.
- C enums carry `*_MAX_ENUM` sentinels (UBSan found out-of-range values to be UB).
- Wire tests use a recording relay; the host can announce another UDP port (`advertised_media_port`).
