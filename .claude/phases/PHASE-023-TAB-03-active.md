# Phase 023 (TAB-03) — USB connections for tablets
Status: ACTIVE

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
- 2026-10-27 — Galaxy S23 Ultra USB tethering does not reach a Mac: Samsung uses RNDIS and macOS has no RNDIS driver (no network interface appears). Windows has one, so Android over USB is a Windows-only path; on a Mac the phone's Wi-Fi hotspot works instead (tested: Mac → S23 Ultra over the phone's hotspot, no router).
- 2026-10-27 — User decision: a USB session starts on the iPad. Its home screen gets a "USB cable" row; tapping it makes the iPad listen, and a sharing Mac or PC on the cable connects within seconds. After a disconnect from either side nothing reconnects by itself.
- 2026-10-27 — The stream-link framing is not negotiated in the handshake (as the task above first said): the connection itself decides it. A host reaches the iPad only through usbmuxd and the iPad's USB listener only takes those connections, so both sides know the link before Hello. Over usbmuxd the host opens the TCP connection; the Noise roles stay viewer = initiator.
