# Phase 031 (AUD-03) — PC host: the PC's sound on the display
Status: ACTIVE

DEVPLAN reference: Phase 17 (part AUD-03). Depends on: PHASE-029 (AUD-01).

## Goal
A PC that extends onto a display can play its sound there.

## Tasks
- [x] Experiment on the user's PC: does the signed VirtualDrivers/Virtual-Audio-Driver install on Windows 11 with Secure Boot and no test mode? Does WASAPI loopback of the speakers keep capturing while the PC's own output is muted? Present the options to the user before building.
- [x] Capture (virtual speaker endpoint, or loopback of the default device) and feed the core's audio API.
- [x] Panel choice per viewer, as on the Mac; the PC's volume and mute apply to the sound it sends (as the Mac's do, user decision 2026-11-23); installer and uninstaller changes if a driver ships.
- [ ] Tests, TESTING.md cases, docs.

## Acceptance Criteria
- [ ] PC → Android TV and PC → Mac: sound plays on the display in step with the picture.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-11-24 — Experiment (OsplayAudioTest, two runs): WASAPI loopback of the default speakers keeps the full level while the endpoint is muted (-18/-23 dB vs -24/-25 dB normal); at 10 % volume it is 23–26 dB quieter, so the PC's volume reaches the captured sound; nothing was flagged silent.
- 2026-11-24 — User decision: loopback + the PC's speakers muted while a display has the sound; no driver. The volume keys work by themselves; the mute key, which would unmute the speakers, is caught, the speakers are muted again and the press mutes/unmutes the display's sound. The speakers' mute is restored on stop and, after a crash, at the next start.
