# Phase 025 (TAB-05) — Android over a USB cable through Android Open Accessory
Status: ACTIVE

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
- 2026-10-28 — User decision: Android side and Mac → Android first; the PC waits for an experiment on the PC (does the device switch to accessory mode, does Windows bind WinUSB to the accessory interface by itself). Windows blocks both steps today: Windows' MTP driver holds the device, so the AOA control requests cannot be sent without replacing its driver; self-signed WinUSB catalogs (libwdi) are reported to fail on Windows 11; an attestation-signed INF needs an EV certificate and still does not solve the first step; UsbDk filters every USB device. Each platform's own USB API (IOUSBHost on the Mac, WinUSB on Windows) instead of libusb: the AOA handshake and link records stay in the core, without an LGPL dependency.
- 2026-10-29 — Mac experiment with a Samsung (04e8:6860, "charging only"): IOUSBHostDevice opens without seizing although AppleUSBACM holds two interfaces; GET_PROTOCOL returns 2; after START the device comes back as 18d1:2d01 (2d00 without USB debugging) with two 512-byte bulk endpoints on interface 0. macOS asks once per new USB identity ("Allow accessory to connect?"); until the user allows it the device is not registered and Android drops back to normal mode after its 10 s accessory timeout, so the host switches again. Bulk OUT and IN time out while no app reads, so the host can wait for the viewer's first bytes.
- 2026-10-29 — User decision: while the Mac/PC shares, an Android device plugged in is switched to accessory mode automatically; Android's own "Open Osplay?" prompt starts the session. After a disconnect nothing reconnects by itself; the Android "computer on the USB cable" row starts it again.
