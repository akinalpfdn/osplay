# Phase 006 (MVP-06) — Transport and session
Status: PENDING

DEVPLAN reference: Phase 5. Depends on: Phase 4.

## Goal
Two `core` instances on the same machine or LAN can open an encrypted session and exchange control messages and media packets.

## Tasks
- [ ] Asio TCP listener/connector for control, UDP media socket, UDP association via session token.
- [ ] Packetizer and reassembler with deadlines, NACK bookkeeping, keyframe request rate limiting.
- [ ] Session state machine, heartbeat, ping/pong with RTT and clock offset estimation, stats aggregation.
- [ ] `capi` for creating host/viewer sessions, pushing access units (host), receiving access units (viewer), callbacks for state, stats and errors, and a log callback.
- [ ] Loopback integration test that streams synthetic access units through a simulated lossy link.

## Acceptance Criteria
- [ ] Loopback test reassembles 100% of frames on a clean link and recovers via NACK or IDR under 5% simulated loss.
- [ ] Session survives a 3-second network interruption and resumes without user action.
- [ ] No unencrypted bytes leave the process after the handshake (verified by a test that inspects the wire).
- [ ] Thread sanitizer and address sanitizer runs are clean on macOS CI.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
