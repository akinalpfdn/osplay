# Phase 029 (AUD-01) — Audio stream in the core
Status: ACTIVE

DEVPLAN reference: Phase 17 (part AUD-01). Depends on: Phase 16.

## Goal
The core carries the host's sound to a viewer next to the video: an Opus stream with its own packets, a jitter buffer and timing that keeps it with the picture, over UDP and over stream links.

## Tasks
- [x] Protocol 1.3: Capabilities flag for "plays audio"; an AudioConfig control message (codec, rate, channels, frame duration); audio packets on the media channel, distinct from video fragments; PROTOCOL.md.
- [x] libopus vendored in core/third_party (BSD), built by CMake and SwiftPM and for the Android NDK; THIRD_PARTY.md.
- [x] Host side: an API to push PCM (48 kHz stereo float), encoded in 10 ms Opus frames; no audio when the viewer does not play it.
- [x] Viewer side: a jitter buffer that hands out PCM in order, conceals a lost packet (Opus PLC), and keeps the delay small (target about 40–60 ms); timestamps on the core clock, so a viewer can line sound up with the picture.
- [x] C API for both sides; Swift (CoreBridge), Windows engine and JNI bindings follow in AUD-02…04.
- [x] Tests: codec round trip, jitter buffer (reordering, loss, late packets, drift), protocol fixtures and fuzzing for the new messages, a session test with audio over UDP and over a stream link.

## Acceptance Criteria
- [x] A core session carries audio from host to viewer with no audible gaps on a clean link, conceals 5% loss, and stays within about 100 ms of the matching video frame (tests).
- [x] Older peers (protocol 1.2) keep working without audio.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-20 — User decision: audio is Phase 17 after the TV phase, on the same branch as Phase 16 (feature/tv-01-tv-hosts); everything goes to main together later. Plan: core (AUD-01), Mac host with a Core Audio process tap that mutes the Mac (AUD-02), PC host after a driver experiment (AUD-03), viewers (AUD-04).
- 2026-11-20 — libopus 1.5.2, float codec only (no DNN/DRED/OSCE, no SIMD), vendored and trimmed (DECISIONS.md).
- 2026-11-20 — Hosts send CELT-only 10 ms stereo frames at 128 kbps without retransmission; "plays audio" goes only to 1.3 peers, whose decoders accept the flag (DECISIONS.md).
- 2026-11-20 — The viewer's audio device pulls PCM from the core's jitter buffer on its own thread: 40 ms reserve that adapts (40–200 ms) and up to 0.2 % resampling for clock drift (DECISIONS.md).
- 2026-11-20 — Opus' BSD license needs a notice in every app binary: the Windows installer stages it now; the Apple apps (AUD-02) and Android (AUD-04) carry it before release.

## Verification
- `core/tests/cpp/audio_test.cpp`: encoder framing, round trip, reordering, 5 % loss without gaps, late packets, stalls, reserve review, ±1000 ppm drift, bursts.
- `session_engine_test.cpp`: sound over the simulated network (clean, 5 % loss), audio 55 ms vs video 22 ms behind capture at 15 ms network latency, AudioStop, a viewer without audio, a 1.2 host.
- `transport_test.cpp`: sound over UDP and over a stream link; `capi_session_test.cpp`: the C API end to end.
- ASan/UBSan and TSan builds pass all 184 core tests.

