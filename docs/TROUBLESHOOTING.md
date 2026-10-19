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

To be documented in Phase 10 (photograph an on-screen millisecond clock together with the viewer).

## Windows viewer: Ctrl+Alt+Shift+V does nothing

Another program registered the same global shortcut first. `OsplayViewerTest` says so when it
starts ("hotkey unavailable"); close the other program or change its shortcut.

## macOS: a Windows viewer cannot connect

macOS asks once whether Osplay may use the local network. If it was denied, enable Osplay in System
Settings › Privacy & Security › Local Network. With the macOS firewall on, also allow incoming
connections for Osplay.
