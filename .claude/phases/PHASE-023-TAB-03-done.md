# Phase 023 (TAB-03) — USB connections for tablets
Status: DONE

DEVPLAN reference: Phase 15 (part TAB-03). Depends on: PHASE-022 (TAB-02).

## Goal
A tablet connected to the computer with a USB cable works as a display with Wi-Fi off.

## Tasks
- [x] Android: viewing over USB tethering (the cable makes an IP network; discovery and media work unchanged); the app explains how to turn tethering on; check tablets without mobile data.
- [x] Media carried on the control stream: a transport mode in `core` for links without UDP, negotiated in the handshake; PROTOCOL.md and the fuzzers cover the new framing.
- [x] iPad over USB: the host reaches the iPad app through usbmuxd (built into macOS; Apple Devices on Windows) and finds USB-connected iPads without Bonjour.
- [x] Mac and Windows hosts list USB-connected tablets next to the nearby computers (as connected viewers once the iPad answers; see decisions).
- [x] Docs: TROUBLESHOOTING (Apple Devices on Windows, tethering), TESTING, PROTOCOL, ARCHITECTURE. README waits for the store listings, like the other tablet sections.

## Acceptance Criteria
- [x] iPad over USB from a Mac, with Wi-Fi off on both (U1, 2026-10-27). From a PC (U2): on the 0.2.0 release draft, user decision.
- [ ] Android over USB tethering from a PC (U6): on the 0.2.0 release draft, user decision. From a Mac it cannot work (RNDIS, see below); the phone's hotspot was tested instead.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-27 — Galaxy S23 Ultra USB tethering does not reach a Mac: Samsung uses RNDIS and macOS has no RNDIS driver (no network interface appears). Windows has one, so Android over USB is a Windows-only path; on a Mac the phone's Wi-Fi hotspot works instead (tested: Mac → S23 Ultra over the phone's hotspot, no router).
- 2026-10-27 — User decision: a USB session starts on the iPad. Its home screen gets a "USB cable" row; tapping it makes the iPad listen, and a sharing Mac or PC on the cable connects within seconds. After a disconnect from either side nothing reconnects by itself.
- 2026-10-27 — The stream-link framing is not negotiated in the handshake (as the task above first said): the connection itself decides it. A host reaches the iPad only through usbmuxd and the iPad's USB listener only takes those connections, so both sides know the link before Hello. Over usbmuxd the host opens the TCP connection; the Noise roles stay viewer = initiator.
