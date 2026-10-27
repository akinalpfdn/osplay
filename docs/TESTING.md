# Manual test matrix

Automated tests cover the core, the engine's portable logic and the apps' view logic (BUILDING.md).
What needs real hardware, operating systems and networks is checked here, on the reference
hardware: a MacBook Air M3 (macOS 26) and a Windows 11 PC with two monitors, on the same network.
Run the matrix before a release and after changes to sessions, displays or power handling.

**Setup.** Pair the two computers once. "UC1" is use case 1: the Mac extends itself onto the PC's
monitors (Mac menu → **Extend this Mac**, PC panel → **Use as display** → the Mac). "UC2" is use
case 2: the PC extends itself onto the MacBook (PC panel → **Extend this PC**, Mac menu → **Use as
display** → the PC). Turn on the stats overlay (Settings) to read frame rate, bitrate and latency.

Record each run in the table at the end: date, build, network (wired or Wi-Fi band), result.

## Sessions

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| S1 | UC1 starts | Start UC1 | One Mac display per offered PC monitor within a few seconds; no pairing code after the first time |
| S2 | UC2 starts | Start UC2 | One PC virtual monitor per offered Mac display, each full screen on its display, with the Windows pointer |
| S3 | Disconnect | **Disconnect** on either side during S1 and S2 | Both sides stop; the host waits for the next viewer; the viewer does **not** reconnect by itself |
| S4 | Hide and show | Viewer shortcut (⌃⌥⌘V / Ctrl+Alt+Shift+V) twice | Displays hide and come back; the host's bitrate drops to almost zero while hidden |
| S5 | Role swap | Swap shortcut (⌃⌥⌘S / Ctrl+Alt+Shift+S) in S1, then again | UC1 becomes UC2 and back, about 4 s each, no pairing code |
| S6 | Revoke | Settings › Devices → **Revoke** during a session | The session ends at once; the next connection is refused without a code |

## Every direction and every monitor

A host makes one virtual display per monitor the viewer offers, in every direction. Before any
PC-host test, Settings › Advanced on the PC must show the helper as ready; "the helper refused this
app" means the installed OsplayHelper is older than the app (BUILDING.md: reinstall it).

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| D1 | Mac → PC | UC1 with every PC monitor selected in the PC panel | One Mac virtual display per PC monitor, each on its monitor |
| D2 | PC → Mac, external display | Connect an external display to the Mac; UC2 with both Mac displays selected | Two PC virtual monitors, one full screen on each Mac display |
| D3 | PC → PC | A Windows laptop extends onto the two-monitor PC (PC panel: **Use as display**) | Two virtual monitors on the laptop, one on each PC monitor |
| D4 | Mac → Mac | A second Mac extends onto this Mac and its external display | One virtual display per offered display of this Mac |
| D5 | Leaving one out | In D2, click one Mac display in the menu's picker (or one monitor in the PC panel in D1), connect | Only the selected displays get a virtual display; the choice is still there after quitting and starting Osplay again |
| D6 | Viewer display unplugged | During D2, unplug the Mac's external display | Only that stream ends: its PC virtual monitor goes away, the other keeps streaming |
| D7 | Clean up on the PC | During D2, **Disconnect**; start D2 again and end the PC app from Task Manager | No Osplay virtual monitor is left in Windows Settings › Display either time |
| D8 | Stream size | In D2 and D4, compare 1080p (default) with Native (**Stream size** next to Quality on the extending computer; applies from the next connection) | 1080p: both displays smooth near 60 fps, text a little softer on a large display. Native: sharpest, needs a strong host and network |

## Several viewers at once

A host extends onto several computers at once; all its viewers share four virtual displays (a Mac
gives one viewer at most two). The PC needs the current OsplayHelper (helper protocol 3).

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| M1 | Mac host, two viewers | This Mac **Extend this Mac**; the PC and a second Mac each **Use as display** and connect to it | Both stream; the menu shows one row per computer (name, displays) with its own Disconnect icon and a row waiting for the next |
| M2 | PC host, two viewers | The PC **Extend this PC**; two Macs each **Use as display** and connect to it | Both stream; the panel shows one row per computer; the PC's own monitors do not stutter |
| M3 | One leaves | In M1 and M2, click the Disconnect icon of one row | Only that computer's displays go; the other keeps streaming without a pause |
| M4 | No display left | Connect viewers until four displays are in use, then connect one more | The last one is turned away before any pairing code ("uses all the displays it can extend onto"); the waiting row says all four are in use |
| M5 | Swap needs one viewer | In M1, try the swap shortcut, and the swap from a viewer | Not offered on the host while two are connected; a viewer's request is answered busy; with one left it works again |

## iPad

Run the iPad app from Xcode (BUILDING.md) or TestFlight. The computer runs Osplay 0.2 or later and
**Extend this Mac** / **Extend this PC**.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| T1 | Mac → iPad | On the iPad, tap the Mac under Nearby computers; confirm the code on both | One Mac display at the iPad's resolution, full screen on the iPad |
| T2 | PC → iPad | The same with the PC | One PC virtual monitor at the iPad's resolution, with the Windows pointer |
| T3 | Turn the iPad | During T1, turn the iPad from landscape to portrait and back | The session starts again each time with the new shape; the Mac display follows |
| T4 | Background | During T1, go to the Home Screen, wait 10 s, open Osplay again | The Mac display goes away while Osplay is in the background and comes back by itself |
| T5 | Old computer | A computer with Osplay 0.1.1 shares its screen | Listed with "Update Osplay on this computer"; tapping it explains, no connection attempt |
| T6 | Revoke | iPad Settings › Paired computers → swipe → Revoke, then connect | Refused at once without a code; Remove makes it ask for a code again |

The same cases apply to the Android app (A1–A6 for T1–T6), run from Android Studio or Google Play
on a phone or tablet with Android 12 or later.

### USB cable

Turn Wi-Fi off on both devices for these. The iPad has trusted the computer once ("Trust This
Computer" when first plugged in); a PC has the Apple Devices app installed.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| U1 | Mac → iPad over USB | Mac: **Extend this Mac**. Connect the iPad with a cable, open Osplay, tap **Computer on the USB cable**; confirm the code on both the first time | Within a few seconds one Mac display at the iPad's resolution, as in T1 |
| U2 | PC → iPad over USB | The same with the PC (**Extend this PC**) | One PC virtual monitor at the iPad's resolution |
| U3 | Turn and background | During U1, turn the iPad, then go to the Home Screen and back | The session starts again each time over the cable, as in T3 and T4 |
| U4 | Disconnect | During U1, tap Disconnect on the iPad; start U1 again and disconnect from the Mac's menu | The display goes away and nothing reconnects until **Computer on the USB cable** is tapped again |
| U5 | Unplug | During U1, pull the cable, wait 15 s, plug it in again | The Mac display goes away within 10 s. If the iPad is not already waiting on the cable, tapping **Computer on the USB cable** brings the session back |
| U6 | Android over USB tethering, PC | Android: Settings › Connections › Mobile Hotspot and Tethering › USB tethering, with Wi-Fi off on both; the PC shares | The PC appears under Nearby computers on the phone; A1 works over the cable |

### Without a router

The PC opens its own network: panel › **No router? Open this PC's network** › **Open network**. The
other device joins it (Wi-Fi settings, or the camera on the QR code) and is on no other Wi-Fi.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| W1 | Open the network | On a PC on no router (Wi-Fi on), open the network | Name, password and QR code appear; closing it makes the network disappear from other devices' lists |
| W2 | PC → PC | The second PC joins the network; one PC extends, the other uses it as a display | Found under Nearby computers; streams as on a router, both ways |
| W3 | PC → Android | The phone joins from the QR code with mobile data on, and keeps the network when Android says it has no internet | The PC appears in the app; A1 works |
| W4 | Mac ↔ PC | The Mac joins from the Wi-Fi menu | Both directions work as D1 and D2 |
| W5 | PC also on a router | Open the network while the PC's Wi-Fi is connected to a router; repeat W2 | Note the stats overlay's bitrate, loss and latency against the same test on the router alone |

## Interruptions

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| I1 | Mac sleeps as viewer | UC2, close the lid (or Apple menu → Sleep), wait 1 min, wake | The PC goes back to waiting; after wake the Mac reconnects by itself and shows the display again |
| I2 | Mac sleeps as host | UC1, sleep the Mac, wait 1 min, wake | The PC shows "Waiting for … to come back" and reconnects by itself once the Mac is awake |
| I3 | PC sleeps as viewer | UC1, Start → Power → Sleep on the PC, wake | The Mac's displays go away; after wake the PC reconnects and they return |
| I4 | PC sleeps as host | UC2, sleep the PC, wake | The Mac waits and reconnects after the PC wakes |
| I5 | Short network outage | UC1, turn Wi-Fi off on either side for 5 s, then on | The stream freezes and resumes in the same session (no reconnect) |
| I6 | Long network outage | UC1, Wi-Fi off for 30 s, then on | The session ends after 10 s; the viewer says it is waiting and reconnects once the network is back |
| I7 | Network change | UC2, switch the Mac from one Wi-Fi network to another in the same LAN | The viewer reconnects by itself |
| I8 | PC monitor resolution | UC1, change one offered PC monitor's resolution or scaling (Windows Settings › Display) | The session restarts by itself; the Mac's display takes the new size |
| I9 | PC monitor unplugged | UC1, unplug one offered monitor | Only that display disappears from the Mac; the other keeps streaming |
| I10 | Mac display change | UC2, change the MacBook's resolution (System Settings › Displays) | The session restarts by itself at the new size |

## Quality and diagnostics

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| Q1 | Bitrate adaptation | UC1 over Wi-Fi; move the PC or Mac far from the router (or limit the Mac with Network Link Conditioner, "Wi-Fi" profile) | The overlay's bitrate drops within a few seconds and the picture stays live; it climbs back when the link recovers |
| Q2 | Stats overlay shortcut | While viewing, press ⌃⌥⌘I (Mac) or Ctrl+Alt+Shift+I (PC) | The overlay appears and disappears; the Settings switch follows |
| Q3 | Copy diagnostics | Settings › Advanced → **Copy diagnostics**, paste into a text editor | Versions, settings and recent log lines; no device names, keys or IP addresses |
| Q4 | Latency | TROUBLESHOOTING.md "Measuring latency" | Median under 40 ms wired, under 60 ms on 5 GHz Wi-Fi |
| Q5 | One-hour session | UC1 with a video playing on one Mac display for 60 min | Memory of Osplay (Activity Monitor, Task Manager) does not grow after the first minutes; the overlay's frame rate stays the same as at the start |

## Results

| Date | Build | Network | Scenarios | Result | Notes |
|------|-------|---------|-----------|--------|-------|
| 2026-10-20 | `3fe19bd` | Wi-Fi | I1–I4 (sleep/wake both ways), S3 (Disconnect then Connect) | Pass | Found and fixed on the way: no connect timeout, frozen frame under Modern Standby, stale port after a session |
| 2026-10-20 | `3fe19bd` | Wi-Fi | I5–I9, Q1, Q2, Q3, Q5 (one hour) | Pass | |
| 2026-10-25 | `d0645b7` | Wi-Fi | D1, D2, D4–D8 (every direction but PC → PC), M1–M3 (several viewers on both hosts) | Pass | Found and fixed on the way: two monitors on one Windows driver device stall the desktop, pooled driver devices shared a stale mode list (issue #30 left open), a Mac host's second viewer took the first one's displays |
| 2026-10-27 | `e00a28d` | Wi-Fi | T1 (Mac → iPad A16) | Pass | The iPad listed the Mac once Local Network access was granted |
| 2026-10-27 | `8d766c6` | Wi-Fi | A1 (Mac → Galaxy S23 Ultra) | Pass | |
| 2026-10-27 | `ed98792` | USB, Wi-Fi off | U1 (Mac → iPad A16 over the cable) | Pass | Pulling the cable ends the session at once |
