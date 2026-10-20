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
| S2 | UC2 starts | Start UC2 | The PC's virtual monitor full screen on the MacBook with the Windows pointer |
| S3 | Disconnect | **Disconnect** on either side during S1 and S2 | Both sides stop; the host waits for the next viewer; the viewer does **not** reconnect by itself |
| S4 | Hide and show | Viewer shortcut (⌃⌥⌘V / Ctrl+Alt+Shift+V) twice | Displays hide and come back; the host's bitrate drops to almost zero while hidden |
| S5 | Role swap | Swap shortcut (⌃⌥⌘S / Ctrl+Alt+Shift+S) in S1, then again | UC1 becomes UC2 and back, about 4 s each, no pairing code |
| S6 | Revoke | Settings › Devices → **Revoke** during a session | The session ends at once; the next connection is refused without a code |

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
