# Phase 029 (AUD-01) — Audio stream in the core
Status: ACTIVE

DEVPLAN reference: Phase 17 (part AUD-01). Depends on: Phase 16.

## Goal
The core carries the host's sound to a viewer next to the video: an Opus stream with its own packets, a jitter buffer and timing that keeps it with the picture, over UDP and over stream links.

## Tasks
- [ ] Protocol 1.3: Capabilities flag for "plays audio"; an AudioConfig control message (codec, rate, channels, frame duration); audio packets on the media channel, distinct from video fragments; PROTOCOL.md.
- [ ] libopus vendored in core/third_party (BSD), built by CMake and SwiftPM and for the Android NDK; THIRD_PARTY.md.
- [ ] Host side: an API to push PCM (48 kHz stereo float), encoded in 10 ms Opus frames; no audio when the viewer does not play it.
- [ ] Viewer side: a jitter buffer that hands out PCM in order, conceals a lost packet (Opus PLC), and keeps the delay small (target about 40–60 ms); timestamps on the core clock, so a viewer can line sound up with the picture.
- [ ] C API for both sides; Swift (CoreBridge), Windows engine and JNI bindings follow in AUD-02…04.
- [ ] Tests: codec round trip, jitter buffer (reordering, loss, late packets, drift), protocol fixtures and fuzzing for the new messages, a session test with audio over UDP and over a stream link.

## Acceptance Criteria
- [ ] A core session carries audio from host to viewer with no audible gaps on a clean link, conceals 5% loss, and stays within about 100 ms of the matching video frame (tests).
- [ ] Older peers (protocol 1.2) keep working without audio.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-20 — User decision: audio is Phase 17 after the TV phase, on the same branch as Phase 16 (feature/tv-01-tv-hosts); everything goes to main together later. Plan: core (AUD-01), Mac host with a Core Audio process tap that mutes the Mac (AUD-02), PC host after a driver experiment (AUD-03), viewers (AUD-04).
