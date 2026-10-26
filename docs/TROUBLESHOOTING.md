# Troubleshooting

> Status: outline. Entries are added as the features they cover land.

## Windows: SmartScreen warns about the installer

Osplay's Windows installer is not code-signed, so SmartScreen shows "Windows protected your PC".
Choose **More info → Run anyway** only if you downloaded the installer from the official GitHub
Releases page. To check the download, compare its hash with `Osplay-Setup.exe.sha256` on the same
release:

```powershell
(Get-FileHash .\Osplay-Setup.exe -Algorithm SHA256).Hash.ToLower()
```

## Content from streaming services appears black

DRM-protected video is blocked from screen capture by both macOS and Windows. This cannot be
worked around.

## macOS: sharing stops with "Screen Recording permission was turned off"

ScreenCaptureKit stops every capture when Screen Recording is revoked, and macOS 15 and later
may ask again for permission periodically. Turn Osplay back on in **System Settings → Privacy &
Security → Screen Recording** and connect again. Development builds signed ad hoc get a new
signature on every build, which macOS treats as a new app; see "Code signing for local
development" in `BUILDING.md`.

## macOS development: encoders take minutes to start

If the app or its tests run from a build folder inside `~/Documents`, `~/Desktop` or
`~/Downloads`, each `VTCompressionSessionCreate` can block for minutes while macOS issues a
sandbox extension for the app bundle to the encoder service (seen on macOS 26). Build into a
folder outside those locations, for example Xcode's default DerivedData
(`~/Library/Developer/Xcode/DerivedData`), by leaving out `-derivedDataPath`.

## Playing a debug stream dump

The debug menu's host media test writes raw H.264 Annex B files (`.h264`) to `~/Movies/Osplay`.
QuickTime Player cannot open raw H.264; use IINA, VLC or `ffplay`.

## Measuring end-to-end latency

The statistics show capture-to-delivery latency, which leaves out decoding and the display. The
whole path is measured with a camera:

1. On the host, open two windows of a millisecond clock that redraws every frame (any browser
   stopwatch with milliseconds): one on the host's own physical screen, one on the shared display.
   Both read the host's clock.
2. Put the host's physical screen and the viewer's screen side by side so one photo shows both
   clocks.
3. Take 20 or more photos (burst mode, 1/250 s shutter or faster). Latency is the host's physical
   screen value minus the viewer's value.
4. Report the median and the worst value, with the network (wired gigabit or 5 GHz Wi-Fi).

DEVPLAN targets: median under 40 ms on wired gigabit, under 60 ms on 5 GHz Wi-Fi.

## Windows viewer: the hide/show shortcut does nothing

Another program registered the same global shortcut first; Osplay says so when the connection
starts. Choose another shortcut in Settings › General (it applies from the next connection), or close
the other program.

## Windows: "Sharing this PC's screen" shows a problem

Sharing needs the OsplayHelper service and the Virtual Display Driver. Settings › Advanced shows
which one is missing; start "OsplayHelper" in Services, or reinstall Osplay, which installs both
again and keeps the paired devices. Logs are in `%LOCALAPPDATA%\Osplay\logs`.

## macOS: a Windows viewer cannot connect

macOS asks once whether Osplay may use the local network. If it was denied, enable Osplay in System
Settings › Privacy & Security › Local Network. With the macOS firewall on, also allow incoming
connections for Osplay.

## The other computer does not appear in the device list

Both computers must be on the same network and subnet (guest Wi-Fi networks often isolate devices).
On the Mac, Osplay needs Local Network access (System Settings › Privacy & Security › Local
Network); without it the list stays empty, or the menu says Bonjour is not allowed. On Windows the
installer allows Osplay through Windows Defender Firewall on every network profile, Public included.
A development build runs from another folder, so Windows asks once instead: allow it for the network
type you are on. `OsplayViewerTest.exe --browse` shows what the PC sees.

If the PC appears but the other computer cannot connect to it ("timed out"), the firewall drops the
connection: check that Windows Defender Firewall › Inbound Rules has an enabled "Osplay" rule, and
run the installer again to restore it.

## An iPad on the USB cable does not connect

On the iPad, tap **Computer on the USB cable** in Osplay; the computer connects only while the iPad
waits there, and only while it shares (**Extend this Mac** / **Extend this PC**). The iPad must trust
the computer: unlock it when plugging in and answer "Trust This Computer". A PC reaches the iPad
through the Apple Mobile Device Service, which the Apple Devices app (Microsoft Store) or iTunes
installs; without it the PC never sees the iPad. A cable that only charges carries no data: Finder
(Mac) or Apple Devices (PC) shows the iPad when the cable works.

## Android over a USB cable

Android's USB tethering creates a network over the cable, and Osplay then works as on Wi-Fi. Windows
supports it; macOS has no driver for the RNDIS tethering Android phones use, so on a Mac no network
appears. With a Mac, turn on the phone's Wi-Fi hotspot and connect the Mac to it: no router is
needed.

## A paired computer is refused

The other side revoked it ("refused this Mac" / "refused this PC"). On that side, remove the device
from Paired devices; the next connection asks for a pairing code again.

## Swap roles does nothing or fails

- "older Osplay without role swap": update both computers; the swap needs protocol 1.1 on both.
- "cannot extend itself right now": the other computer lacks what hosting needs — Screen Recording on
  a Mac, the OsplayHelper service and the Virtual Display Driver on Windows (Settings › Advanced
  there says which).
- "did not appear after the swap": the new host could not advertise (Local Network permission on the
  Mac). Pick it under Nearby computers once it shows up.
- The shortcut is ignored: another program owns it; the menu says so. Use the "Swap roles" button or
  record another shortcut in Settings.

## The viewer keeps "waiting for … to come back"

After sleep or a network drop the viewer reconnects when the host advertises itself again. If it
does not: check that the host is awake and still in its sharing role, and that both are on the same
network (Local Network permission on the Mac, a Private network on Windows). Choosing the computer
under Nearby computers, or another role, ends the wait. Copy diagnostics (Settings › Advanced) for a
bug report.

## A Mac on Wi-Fi drops frames on an otherwise quiet network

macOS uses AWDL (Apple Wireless Direct Link) for AirDrop, Sidecar, Universal Control and AirPlay
to a Mac. To listen for nearby devices it moves the Mac's Wi-Fi to other channels several times a
second, and packets sent meanwhile arrive late or are lost. Osplay marks its traffic as interactive
video, which gives it Wi-Fi priority; if the overlay still shows loss or keyframe requests with a
Mac on Wi-Fi, turn AWDL off for the session on each Mac involved:

```bash
sudo ifconfig awdl0 down
```

AirDrop, Sidecar and Universal Control stop working until `sudo ifconfig awdl0 up` or a restart.
Measured on 2026-10-25 (Mac → Mac over Wi-Fi, two 1080p streams): with AWDL on, frames were
resent and dropped every few seconds; with it off, none were.

