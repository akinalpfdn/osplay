# Phase 025 (TAB-05) — Android over a USB cable through Android Open Accessory
Status: PENDING

DEVPLAN reference: Phase 15 (part TAB-05). Depends on: PHASE-024 (TAB-04).

## Goal
An Android 12+ phone or tablet works as a display for a Mac or a PC over a USB cable, with no network, tethering or developer options, the way the iPad does over usbmuxd.

## Tasks
- [ ] Research first: binding WinUSB to the accessory-mode device (18D1:2D00/2D01) on Windows — a signed driver package in the installer or another route — and how the helper or installer installs it. Present the options to the user before building.
- [ ] Core: the computer side over libusb — find Android devices, switch them to accessory mode (AOA control requests with Osplay's manufacturer, model and version strings), open the bulk endpoints and carry stream link records over them; a watcher that reports attached and detached devices like the usbmuxd one.
- [ ] Android: Osplay as a USB accessory (accessory filter, the system's "open Osplay" prompt), the viewer session over the accessory's streams, a "USB cable" choice on the home screen like the iPad's.
- [ ] Mac and PC apps: start a host session on an attached accessory like the iPad USB flow; no reconnect after a disconnect from either side.
- [ ] Keep USB tethering as a phone ↔ PC fallback; update the Android hint once AOA works.
- [ ] Tests: core tests with a fake libusb device; TESTING.md cases for Mac → Android and PC → Android over the cable, unplug and disconnect.
- [ ] Docs: ARCHITECTURE, TESTING, TROUBLESHOOTING, BUILDING (libusb), THIRD_PARTY (libusb, LGPL-2.1: dynamic linking).

## Acceptance Criteria
- [ ] Mac → Android tablet and PC → Android tablet over a USB cable, Wi-Fi off on both, on a tablet without tethering.
- [ ] Unplugging ends the session within 10 s; nothing reconnects until the user chooses the cable again.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-27 — User decision: add AOA after TAB-04. TAB-03 used USB tethering because it needed no code, but it reaches only a PC (macOS has no driver for Android tethering) and only devices with a tethering menu; Wi-Fi-only tablets usually have none.
