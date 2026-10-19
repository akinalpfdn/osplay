# Troubleshooting

> Status: outline. Entries are added as the features they cover land.

## Windows: SmartScreen warns about the installer

Osplay's Windows builds are not code-signed yet, so SmartScreen shows "Windows protected your PC".
Choose **More info → Run anyway** only if you downloaded the installer from the official GitHub
Releases page. Signing through SignPath Foundation is planned.

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

## Windows viewer: Ctrl+Alt+Shift+V does nothing

Another program registered the same global shortcut first. `OsplayViewerTest` says so when it
starts ("hotkey unavailable"); close the other program or change its shortcut.

## macOS: a Windows viewer cannot connect

macOS asks once whether Osplay may use the local network. If it was denied, enable Osplay in System
Settings › Privacy & Security › Local Network. With the macOS firewall on, also allow incoming
connections for Osplay.

## The other computer does not appear in the device list

Both computers must be on the same network and subnet (guest Wi-Fi networks often isolate devices).
On the Mac, Osplay needs Local Network access (System Settings › Privacy & Security › Local
Network); without it the list stays empty, or the menu says Bonjour is not allowed. On Windows the
network profile must be **Private**, and Windows Defender Firewall asks once whether Osplay may
accept connections: allow it for private networks. `OsplayViewerTest.exe --browse` shows what the
PC sees.

## A paired computer is refused

The other side revoked it ("refused this Mac" / "refused this PC"). On that side, remove the device
from Paired devices; the next connection asks for a pairing code again.
