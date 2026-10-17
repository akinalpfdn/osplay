# Architecture

> Status: outline. Sections are filled in by the phases that build each part; the full design is in
> [`DEVPLAN.md`](../DEVPLAN.md) → Architecture.

## Overview

Every installation can act as a **host** (owns virtual displays, captures, encodes, sends) or a
**viewer** (receives, decodes, presents full screen). One session connects one host to one viewer
and carries one video stream per virtual display.

```
HOST                                                         VIEWER
Virtual display ─► Capture ─► Encoder ─► core: packetize ──UDP──► core: reassemble ─► Decoder ─► Presenter
                                              ▲                        │
                                              └──── TCP control ◄──────┘
```

## Repository layout

| Path | Contents |
|------|----------|
| `core/` | Shared C++20 library: protocol, crypto, transport, media, session, flat C API (`core/include/osplay/osplay.h`) |
| `Package.swift` | Swift package: the C++ core as the `OsplayCore` module and its Swift wrapper `CoreBridge` (`macos/CoreBridge/`) |
| `macos/` | Swift menu bar app, generated with XcodeGen from `macos/project.yml` |
| `windows/engine/` | Native engine DLL (`OsplayEngine.dll`): capture, encode, decode, present, discovery |
| `windows/helper/` | `OsplayHelper` Windows service for privileged virtual display driver operations |
| `windows/app/` | WPF shell (`Osplay.App`) and its string resources (`Osplay.Localization`) |
| `windows/installer/` | Inno Setup script |
| `protocol/` | Binary fixtures shared by every test suite |
| `docs/` | This documentation |

## Build graph

- `core` is a static library. CMake (`core/CMakeLists.txt`) is the reference build; SwiftPM
  (`Package.swift`) compiles the same sources for the macOS app.
- The macOS app links `core` through the local Swift package's `CoreBridge` product; Swift code
  never calls the C API directly.
- `core/src/protocol/` implements `docs/PROTOCOL.md`: control messages (`control_messages.hpp`)
  and media packets (`media_packets.hpp`), with one validation function per message shared by the
  encoder and the decoder. Media decoding does not allocate; payloads are views into the datagram.
- `core/src/crypto/` implements PROTOCOL.md → Encryption: Monocypher wrappers (`primitives`), the
  Noise XX `HandshakeState` and `CipherState` (`noise`), and media datagram sealing with a replay
  window (`media_crypto`). Randomness comes from the platform through `crypto::RandomSource`.
- `core/src/media/` splits access units into fragments (`packetizer`), keeps recent frames for
  retransmission (`retransmit_cache`) and rebuilds frames on the viewer (`reassembler`: in-order
  delivery, NACK, deadline, wait for keyframe).
- `core/src/session/` is the session protocol without I/O (`SessionEngine`): bytes, datagrams,
  time and commands go in; bytes, datagrams and observer callbacks come out. Tests drive it with a
  simulated lossy network and clock. Every tunable is in `session_config.hpp`.
- `core/src/transport/` runs one `SessionEngine` over Asio sockets (`AsioSession`): one I/O thread
  per session owns the TCP control connection, the UDP media socket and a 2 ms timer. Callbacks run
  on that thread; commands from other threads are posted to it.
- `core/src/capi/` is the flat C API. `osplay_session` wraps an `AsioSession` and forwards its
  callbacks to C function pointers.
- `windows/CMakeLists.txt` adds `core` as a subdirectory and links it into the engine DLL.
- The WPF shell loads the engine DLL through P/Invoke (from Phase 12).

## Virtual displays on macOS

`VirtualDisplayProviding` (`macos/Osplay/Services/VirtualDisplay/`) is the only way the app creates
displays. Its implementation, `CGVirtualDisplayProvider`, is the only code that touches the private
`CGVirtualDisplay` API declared in `CGVirtualDisplayPrivate.h`. Resolution listing and switching
use public CoreGraphics API (`DisplayModes`).

Behavior measured on a MacBook Air M3, macOS 26.6, lid open (Phase 1):

| Observation | Consequence |
|-------------|-------------|
| Two virtual displays run at the same time next to the built-in screen; the one-external-display hardware limit does not apply. | Use case 1 is feasible. |
| Releasing the `CGVirtualDisplay` object removes the display immediately; macOS moves its windows. | Display lifetime = handle lifetime. |
| Every mode `W×H` passed in the settings yields a 1× resolution and, with `hiDPI = 1` and a `2W×2H` backing within `maxPixels`, a 2× "looks like W×H" resolution. macOS adds its own standard sizes. | Modes are "looks like" sizes: native, ¾ and ½ of the remote monitor. `maxPixels` is twice the native size when HiDPI is on. |
| On a display identity it has not seen, macOS picks the largest 2× mode: a 2560×1440 monitor starts at a 5120×2880 backing. | Osplay activates the "crisp" HiDPI resolution (looks like half size, backing = remote pixels) when it wants a default, instead of rendering and scaling four times the pixels. |
| Vendor, product and serial number identify the display; a recreated display with the same identity returns to its previous position and resolution. | Identity is derived from the peer ID and remote monitor index (FNV-1a, never serial 0). |
| `applySettings` on a live display changes its modes without changing its display ID. | Remote monitor changes do not recreate the display. |
| A process that has already read display information (every AppKit app does at launch) sees **no modes** for a display it creates later, until it processes window server notifications on its run loop — a few hundred milliseconds in an app. A command-line process without `NSApplication` never sees them, although other processes do. | `VirtualDisplayHandle.waitUntilReady()` waits (asynchronously, with a timeout) before any resolution is read or changed. Displays are created first and configured afterwards, so one slow display does not block the other. |

## Virtual Display Driver control surface (Windows)

Findings from reading the driver source (VirtualDrivers/Virtual-Display-Driver, `master` at
`d7244969`, file `Virtual Display Driver (HDR)/MttVDD/Driver.cpp`), Phase 2:

| Aspect | Behavior |
|--------|----------|
| Device | Root-enumerated, hardware ID `Root\MttVDD` (`MttVDD.inf`), UMDF IddCx driver. |
| Settings file | `vdd_settings.xml` in the directory named by `HKLM\SOFTWARE\MikeTheTech\VirtualDisplayDriver\VDDPATH`, default `C:\VirtualDisplayDriver`. Elements used: `monitors/count`, `gpu/friendlyname` (DXGI adapter description or `default`), `resolutions/resolution/{width,height,refresh_rate}`, `global/g_refresh_rate` (multiplies every resolution). |
| When settings are read | Only in `EvtDriverDeviceAdd` (`loadSettings()`), i.e. when the device starts. |
| Preferred mode | The first resolution in the file (`PreferredMonitorModeIdx = 0`). |
| Monitor count | `count = 0` is coerced to 1. A monitor cannot be removed by count. |
| Named pipe | `\\.\pipe\MTTVirtualDisplayPipe`, plain-text commands (`RELOAD_DRIVER`, `SETDISPLAYCOUNT n`, `SETGPU "name"`, `PING`, `GETSETTINGS`, logging and colour toggles). Its ACL is `D:(A;;GA;;;WD)` — any local user. |
| `RELOAD_DRIVER` / `SETDISPLAYCOUNT` | Call `InitAdapter()` without re-reading settings; new counts or modes in the file are not applied until the device restarts. |

Consequences for Osplay:

- Every configuration change is **write the file, then (re)start the device**; ending a session is
  **disable the device**, which removes the monitor. Both need administrator rights, hence the
  helper service.
- Osplay never uses the driver's pipe: it cannot change modes, and it is open to every user.
- Osplay owns `vdd_settings.xml` and rewrites it before every session: one monitor, one mode (the
  viewer's exact pixel size and refresh rate), no global refresh rates, hardware cursor on (the
  pointer stays out of captured frames and is sent separately).
- Osplay takes over the driver: other software using the same VDD installation will see its
  monitor disabled outside Osplay sessions.

## OsplayHelper service (Windows)

`windows/helper/` is a LocalSystem service. It is the only Osplay component with administrator
rights and accepts four commands from the engine:

| Command | Effect |
|---------|--------|
| Configure (width, height, refresh, GPU name) | Validates the values, writes `vdd_settings.xml`; restarts the device if a monitor is present. |
| Enable | Enables the device: the monitor appears with the configured mode. Requires a prior Configure. |
| Disable | Disables the device: the monitor disappears. Persistent across reboots. |
| Status | Reports whether the driver is installed, the monitor present and the configuration. |

Safety rules:

- **One client at a time.** A second engine that connects while a session is active waits up to
  2 s and then gets `OSPLAY_ENGINE_ERROR_HELPER_BUSY`.
- **The monitor exists only while the client is connected.** The pipe accepts one client; when it
  disconnects for any reason (session end, engine crash), the console user logs off or switches,
  or the service stops, the helper disables the device. On service start it disables the device as
  well, so a monitor left behind by a crash or by the driver's installer goes away.
- **Only the console user can connect.** The pipe is created with `FILE_FLAG_FIRST_PIPE_INSTANCE`
  (no squatting), `PIPE_REJECT_REMOTE_CLIENTS` and the DACL `D:P(A;;GA;;;SY)(A;;GRGW;;;<console
  user SID>)`. After connecting, the helper checks again that the client's session is the active
  console session and its token user is the console user (or SYSTEM).
- **The engine checks the server.** The pipe server must run in session 0, where only services
  run; otherwise the engine refuses it (`OSPLAY_ENGINE_ERROR_HELPER_NOT_TRUSTED`).
- Every value is bounds-checked (width 640–7680, height 480–4320, refresh 24–240 Hz, GPU name ≤ 128
  bytes of UTF-8 without control characters). Rejected requests are logged to
  `%ProgramData%\Osplay\logs\helper.log` with the reason.

### Pipe protocol

All integers little-endian; one request and one response per pipe message; 512 bytes maximum.
Implemented once in `windows/shared/src/helper_protocol.cpp`.

```
Request:  magic u32 = "OSPH" | version u16 = 1 | command u16 | payload_length u32 | payload
Response: magic u32 = "OSPH" | version u16 = 1 | result u16  | payload_length u32 | payload

Configure (command 1) payload:
  width u32 | height u32 | refresh_millihertz u32 | gpu_name_length u16 | gpu_name (UTF-8)
Enable (2), Disable (3), Status (4): no payload

Response payload (always 16 bytes):
  vdd_installed u8 | device_enabled u8 | configured u8 | reserved u8 |
  width u32 | height u32 | refresh_millihertz u32

Result codes: 0 Ok, 1 MalformedRequest, 2 UnsupportedVersion, 3 UnknownCommand,
  4 ValueOutOfRange, 5 VddNotInstalled, 6 DeviceControlFailed, 7 ConfigWriteFailed, 8 NotConfigured
```

### Engine API

`windows/engine/include/osplay/engine.h` exposes the virtual display as an opaque handle:
`osplay_virtual_display_open` (connects to the helper), `_create_monitor`, `_remove_monitor`,
`_status`, `_close`. Inside the engine, `VirtualDisplayProvider` is the interface;
`HelperVirtualDisplayProvider` implements it over a `HelperChannel` (the named pipe in production,
a fake in tests).

Creating a monitor is three steps: the helper writes the settings (Configure) and enables the
device (Enable); then the engine sets the mode. Windows gives a new monitor its own default mode
(800×600 for this driver, measured on the developer PC) even when the driver offers only the
requested one, and display settings belong to the user's session, which a session-0 service cannot
change. `GdiDisplayModeApplier` therefore finds the monitor — the active CCD path whose adapter
interface path starts with the driver device's instance ID (`ROOT\DISPLAY\0000` →
`\\?\ROOT#DISPLAY#0000#`) — waits up to 5 s for it to join the desktop, applies the mode with
`ChangeDisplaySettingsExW` and reads it back. If that fails, the monitor is removed again.
