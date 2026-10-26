# Phase 023 (TAB-03) — USB connections for tablets
Status: PENDING

DEVPLAN reference: Phase 15 (part TAB-03). Depends on: PHASE-022 (TAB-02).

## Goal
A tablet connected to the computer with a USB cable works as a display with Wi-Fi off.

## Tasks
- [ ] Android: viewing over USB tethering (the cable makes an IP network; discovery and media work unchanged); the app explains how to turn tethering on; check tablets without mobile data.
- [ ] Media carried on the control stream: a transport mode in `core` for links without UDP, negotiated in the handshake; PROTOCOL.md and the fuzzers cover the new framing.
- [ ] iPad over USB: the host reaches the iPad app through usbmuxd (built into macOS; Apple Devices on Windows) and finds USB-connected iPads without Bonjour.
- [ ] Mac and Windows hosts list USB-connected tablets next to the nearby computers.
- [ ] Docs: README, TROUBLESHOOTING (Apple Devices on Windows, tethering), TESTING.

## Acceptance Criteria
- [ ] iPad over USB from a Mac and from a PC, with Wi-Fi off on both.
- [ ] Android tablet over USB tethering from a Mac and from a PC, with Wi-Fi off on both.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
