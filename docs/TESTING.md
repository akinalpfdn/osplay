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
| U7 | Mac → Android over USB | Mac: **Extend this Mac** with the Android device plugged in (plug it in again first if it was in Osplay's mode). Answer Android's "Open Osplay?" with OK; the first time, also macOS's "Allow accessory to connect?" | Within a few seconds one Mac display on the Android device, as in A1 |
| U8 | Android disconnect | During U7, tap Disconnect on Android, then **Computer on the USB cable**; again, disconnecting from the Mac's menu | The display goes away and nothing reconnects; the cable row brings it back within seconds |
| U9 | Android unplug | During U7, pull the cable, wait 15 s, plug it in again | The display goes away; Android waits for the cable, asks "Open Osplay?" again and the display comes back |
| U10 | Android Studio open | U7 with USB debugging on and Android Studio (adb) running on the Mac | The same as U7 |
| U11 | PC → Android over USB | On a PC with Osplay installed by the installer (no Zadig): **Extend this PC** with the Android device plugged in, USB debugging off. Answer "Open Osplay?" with OK | Within a few seconds one PC virtual monitor on the Android device; Device Manager shows "Osplay Android USB" |
| U12 | PC: Android disconnect and unplug | U8 and U9 with the PC | As U8 and U9; the virtual monitor goes away within 10 s of unplugging |
| U13 | PC: USB debugging on | U11 with USB debugging on | No "Open Osplay?"; the panel says USB debugging is on; adb keeps working |
| U14 | PC: install and uninstall | Install on a PC without UsbDk, restart, run U11, then uninstall | The installer adds UsbDk and the accessory driver without asking and without a restart prompt; its last page says the cable works after one restart; the uninstaller removes both, the certificate "Osplay Android USB driver (this computer only)" from the machine's Trusted Root and Trusted Publishers stores, and leaves a UsbDk that was installed before Osplay. Installing again before restarting shows no error; the last page asks for a restart and another run |

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
| W6 | Stays connected | Join a phone and leave it idle for five minutes, then stream to it | Still joined and streams; Event Viewer › WLAN-AutoConfig shows no "Wireless security stopped" for it |
| W7 | Settings come back | Note the name in Settings › Mobile hotspot, open and close Osplay's network | The hotspot is off and shows your own name again |

## Android TV

The Android app on an Android TV or Google TV with Android 12 or later, installed with
`adb install` (BUILDING.md) or from Google Play, driven with the TV's remote. Turn the stats
overlay on in the app's Settings for TV4.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| TV1 | Launcher | Open the TV's apps row | Osplay's blue banner is there; it opens the list of computers with no USB cable section |
| TV2 | Mac → TV | Choose the Mac with the remote (each row shows a ring when focused) and confirm the code | A Mac display in the TV's mode (3840×2160 on a 4K TV) that looks like 1920×1080 |
| TV3 | PC → TV | The same with the PC | A virtual monitor in the TV's mode at 200% in Settings › Display (100% on a 1080p TV) |
| TV4 | Native stream | On the computer set the stream size to Native, then TV2 | The overlay shows close to 60 fps; on a TV whose decoder stops at 4K30 the stream is smaller instead, still near 60 fps |
| TV5 | Remote | During TV2 press OK, move along the controls, press Back twice | OK shows the controls with the stats button focused; they stay while focused; the first Back hides them, the second ends the session and shows the list |
| TV6 | Home button | During TV2 press the remote's Home button, wait 10 s, open Osplay again | The display goes away on the computer and comes back when Osplay opens |

## Apple TV (simulator)

The `OsplayTV` scheme in a tvOS simulator on the Mac (BUILDING.md); the arrow keys, Return and
Escape (Menu) drive the remote. There is no Apple TV to test on, so the decoder, the HDMI output and
latency on hardware are not covered.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| ATV1 | Mac → Apple TV 4K | Simulator "Apple TV 4K"; the same Mac shares; choose it and confirm the code on both | A 3840×2160 Mac display that looks like 1920×1080 shows in the simulator |
| ATV2 | PC → Apple TV | The PC shares; choose it in the simulator | A PC virtual monitor at 200% |
| ATV3 | Apple TV 1080p | Simulator "Apple TV 4K (at 1080p)"; ATV1 | A 1920×1080 display at full size |
| ATV4 | Remote | During ATV1 press Return, move along the controls, press Escape twice | Return shows the controls and focuses the statistics button; the first Escape hides them, the second ends the session |
| ATV5 | Wording | Home screen and Settings | They say Apple TV, never iPad; no USB cable section |

## TVs (simulated)

A Debug build of the Mac app poses as a TV when it is used as a display: menu › Developer tools ›
**Simulated TV**. It offers one monitor of unknown size at 200% instead of its own displays and
shows the stream full screen on the first one. Turn the stats overlay on (Settings) to read the
frame rate and bitrate.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| V1 | Mac → 4K TV | Viewer Mac: **4K TV, 60 Hz**. Host Mac extends onto it | A 3840×2160 display on the host that looks like 1920×1080 (crisp HiDPI); the overlay shows about 60 fps |
| V2 | PC → 4K TV | V1 with the PC as host | A 3840×2160 virtual monitor at 200% in Settings › Display; the overlay as in V1 |
| V3 | Decoder limit | Viewer Mac: **4K TV, decoder up to 30 Hz**, stream size Native on the host; repeat V1 and V2 | The display stays 3840×2160 at 60 Hz on the host (System Settings › Displays, Settings › Display); the overlay shows about 30 fps and about half V1's bitrate |
| V4 | 1080p TV | Viewer Mac: **1080p TV**; Mac and PC host | A 1920×1080 display at 200% (looks like 960×540 on the Mac) |
| V5 | Back to normal | Viewer Mac: **Off**; connect again | The Mac's own displays are offered, at their own sizes and scales |

## Sound

The Mac sends its sound to a display (AUD-02). Until the viewers play it (AUD-04) the Mac side is
checked with a Debug build: menu › Developer tools › **Host media test**, which streams to a viewer
inside the same app and shows what that viewer hears.

| # | Scenario | Steps | Expected |
|---|----------|-------|----------|
| S1 | Permission | Fresh install: onboarding › System Audio Recording › Allow | macOS asks once; the row shows a check mark. Osplay works without it; only sound needs it |
| S2 | Sound to the test viewer | Host media test › Start, then **Send this Mac's sound to the test viewer**; play music | The Mac falls silent; the sound line shows a level (for example −20 dB), about 130 kbps and a 30–50 ms reserve |
| S3 | Sound back | S2, turn the switch off (or Stop) | The music plays on the Mac again within a second |
| S4 | Output change | S2, plug in or pick other headphones as output | The Mac stays silent and the level line keeps moving |
| S5 | Older macOS | On macOS 14.0–14.1, connect a viewer that plays sound | No sound button; the row says sound on the display needs macOS 14.2 |
| S6 | Mac → PC | UC1; on the Mac, press the speaker button in the PC's row; play music on the Mac | The Mac falls silent and the music plays from the PC's speakers, in step with the picture |
| S7 | PC speakers change | S6, unplug or switch the PC's default output | The sound continues on the new default output within a second |
| S8 | Sound back | S6, press the button again, or disconnect | The music plays on the Mac again; the PC releases its speakers |
| S9 | Mac → Mac, iPad, Apple TV | Connect a second Mac, the iPad or the Apple TV as a display; press the speaker button in its row; play music | The music plays on that device, in step with the picture; the stats overlay shows a sound line |
| S10 | iPad interruption | S9 on the iPad, start and end a FaceTime call or Siri | The sound comes back on its own after the interruption |
| S11 | Mac viewer output change | S9 on a Mac, switch its output (headphones, display speakers) | The sound continues on the new output within a second |

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
