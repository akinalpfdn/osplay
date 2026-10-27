# Architecture

> Status: outline. Sections are filled in by the phases that build each part; the full design is in
> [`DEVPLAN.md`](../DEVPLAN.md) → Architecture.

## Overview

Every installation can act as a **host** (owns virtual displays, captures, encodes, sends) or a
**viewer** (receives, decodes, presents full screen). One session connects one host to one viewer
and carries one video stream per virtual display. A host runs a session per viewer, so it can
extend onto several computers at once; all its viewers share four virtual displays (a Mac gives one
viewer at most two), a new viewer gets the ones left and is turned away as busy before pairing when
none is (DECISIONS.md, 2026-10-25).

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
| `macos/` | The Apple apps, generated with XcodeGen from `macos/project.yml`: the macOS menu bar app (`Osplay/`), the iPad viewer app (`OsplayPad/`) and the code both compile (`Shared/`) |
| `android/` | Android viewer app (Kotlin, Jetpack Compose) with the core built by the NDK and a JNI bridge (`app/src/main/cpp/`) |
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
  on that thread; commands from other threads are posted to it. On macOS the media socket is
  marked as interactive video and the control socket as signaling (`traffic_class.cpp`), so Wi-Fi
  treats both as latency sensitive.
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

## Host media pipeline on macOS

`HostSession` (`macos/Osplay/Features/Host/`) is an actor that owns one host session: the core
session (`CoreSession` from CoreBridge), the virtual displays standing in for the viewer's
monitors, and one `StreamPipeline` per display. `ConnectionViewModel` runs one per viewer and one
more that listens for the next; they draw their displays from one `DisplayBudget`, each through its
own `VirtualDisplayLender`, and every session notes its peer's key (`SessionPeer`) from its trust
query. Pairing codes are shown one at a time.

```
ViewerMonitors ─► VirtualDisplayLender ─► StreamConfig per display ─► core
                         │
          ScreenCaptureKit (420v IOSurface) ─► VideoToolbox H.264 ─► Annex B ─► core push
```

- **Displays.** `VirtualDisplayLender` creates one virtual display per offered monitor (at most
  two, and only as many as the budget has left), waits until it is ready and activates the crisp
  HiDPI resolution, so the capture backing equals the remote monitor's pixels. A session returns its
  own displays when it ends; the other viewers keep theirs.
- **Stream size.** Each stream is fitted inside the user's stream size setting (720p, 1080p by
  default, 1440p or Native) with `osplay_fit_stream_size`, so a large monitor is captured from its
  full-size virtual display and encoded smaller; the bitrate follows the stream's pixels.
- **Capture** (`Services/ScreenCapture/`). One `SCStream` per display at the stream's pixel size
  and refresh rate, 420v pixel format, BT.709, cursor composited. Only samples with status
  complete or started are frames; an unchanged screen produces nothing and the viewer keeps its
  last frame. `stream(_:didStopWithError:)` ends the session with a mapped reason; revoking Screen
  Recording arrives as `permissionDenied`.
- **Encode** (`Services/VideoEncoding/`). One `VTCompressionSession` per stream with low-latency
  rate control (realtime fallback without it), no frame reordering, a 60 s safety-net keyframe
  interval and keyframes on request. Output is converted from AVCC to Annex B with SPS and PPS in
  front of every keyframe. Pixels are never copied: the IOSurface goes from ScreenCaptureKit to
  VideoToolbox; only the encoded bitstream is copied into the core.
- **Keyframes on idle screens.** A keyframe request re-encodes the last captured frame at once,
  so a viewer that joins or recovers from loss does not wait until something on screen moves.
- **Threading.** Each pipeline runs capture callbacks and encode calls on its own serial queue;
  encoder output is pushed to the core from VideoToolbox's thread (`push` only queues). Session
  events are handled on the actor; nothing in the frame path hops through it.
- **Debug dump.** With a dump directory, `StreamDump` appends each stream's Annex B output to
  `osplay-<time>-stream<N>-<W>x<H>.h264` on a background queue (playable with IINA, VLC, ffplay).
  The debug menu's "Host media test" runs the whole pipeline over loopback into an in-process
  viewer and shows pacing, bitrate and latency per stream.

## macOS app UI

A menu bar app (`LSUIElement`) with three surfaces, all built from `Theme` tokens and the string
catalog (English and Turkish):

- **Menu bar window** (`Features/Menu`): the role (off, share screens, use as display), this
  Mac's displays to offer when used as a display (drawn where they sit, like the Windows panel's
  monitor picker), nearby computers with their paired state, the current session with Disconnect,
  the quality preset, and links to Settings and Permissions. `ConnectionViewModel` (`Features/Connection`) owns discovery,
  sessions and pairing behind it.
- **Settings** (`Features/Settings`, SwiftUI `Settings` scene): General (launch at login through
  `SMAppService`, recordable hide/show shortcut, permissions), Displays (per-device arrangement
  canvas and HiDPI choice; arrangements snap with `DisplayLayout` and are applied with
  `DisplayArranger`), Devices
  (rename, revoke, remove), Advanced (stats overlay, log export to `~/Library/Logs/Osplay`, core
  version). `AppSettings` keeps the values in `UserDefaults`; the host reads quality and HiDPI when
  a viewer's monitors arrive, the viewer reads the overlay switch with every statistics update.
- **Permissions window** (`Features/Onboarding`): Screen Recording and Local Network with live
  status, opened at first launch and whenever Screen Recording is missing. Local Network has no
  query API; a Network.framework Bonjour browse reports a policy denial, which is what it shows.

The pairing code appears in a floating panel (`Features/Pairing`) wherever the user is, because the
menu bar window is usually closed.

## Viewer pipeline on macOS

`ViewerSession` (`macos/Shared/Viewer/`) is an actor that owns one viewer session: the
core session, and per stream a `ViewerStreamPipeline` (decoder) and a presenter.

```
core access unit ─► decode queue ─► VideoToolbox ─► AVSampleBufferVideoRenderer (display immediately)
core cursor shape/position ─► main actor ─► cursor layer above the video
```

- **Displays** (`Services/FramePresenting/LocalDisplays.swift`). Every attached display, up to
  four, in physical pixels; ids follow the arrangement, left to right, as on Windows. The menu's
  picker leaves displays out (`AppSettings.excludedDisplays`, keyed by name and place). Each stream
  is shown on the display whose monitor it extends onto; a stream for a monitor that was not offered
  is stopped on the host. During a session displays are matched by `CGDirectDisplayID`: a viewed
  display with a new size, refresh rate or scale starts the session again, one that is unplugged
  ends only its own stream (unless it was the last one, which starts the session again too).

- **Decode** (`macos/Shared/VideoDecoding/`). Annex B is split into NAL units; the SPS and PPS in
  front of each keyframe build the format description, slices go to a realtime
  `VTDecompressionSession` as 4-byte length-prefixed samples. Output is IOSurface-backed. After
  any error (invalid data, decode failure) frames are dropped until the next keyframe, and the
  pipeline asks the core for one (`osplay_session_request_keyframe`, at most one per 100 ms
  locally and per 250 ms in the core).
- **Present** (`Services/FramePresenting/`). Frames go from the decoder's thread straight to the
  layer's renderer with `DisplayImmediately`; nothing waits for the main actor. Full screen is a
  borderless window above the menu bar and Dock of the target display; the local pointer hides
  after 2 s without movement. Separate-cursor streams draw the host's cursor in a layer mapped
  onto the aspect-fitted video.
- **Visibility.** ⌃⌥⌘V (Carbon `RegisterEventHotKey`, no Accessibility permission) hides or shows
  the viewer from anywhere; hidden streams are paused so the host stops sending. The hotkey runs
  on the main run loop, which no frame work touches, so it works even when frames stop.
- **Power.** While a stream is visible, an `IOPMAssertion` prevents display sleep and a
  latency-critical activity disables App Nap.
- **Debug loopback.** The menu's "Viewer loopback" hosts a 1920×1080 virtual display and views
  it on the same Mac (window or full screen), with corruption injection and a frame freeze.

## iPad app

The iPad is a viewer only (DEVPLAN Phase 15): iPadOS gives apps no system virtual displays, and no
touch or other input is forwarded. `macos/Shared/` holds what it shares with the Mac app — the
viewer session and decoder, Bonjour discovery, the Keychain key store and paired devices, the
sample-buffer rendering helpers and the shared tunables — compiled into both app targets
(DECISIONS.md, 2026-10-26).

- **One screen, one monitor** (`OsplayPad/Services/Screen/`). The iPad offers its screen in the
  orientation it is held, in native pixels at 200% scale. Turning the iPad starts the session
  again with the new shape; `UIRequiresFullScreen` keeps the app out of smaller windows.
- **Present** (`OsplayPad/Services/Presenting/`). The stream's view — `AVSampleBufferDisplayLayer`
  plus a cursor layer for Windows hosts and the statistics label — is embedded full screen by the
  viewer screen; a tap shows Disconnect and the statistics toggle for a few seconds.
- **Lifecycle.** While frames are shown the idle timer is off. Going to the background ends the
  session cleanly (the host removes its display), and the app connects to the same computer again
  when it returns; a dropped connection or sleeping computer is retried the same way as on the Mac.
- **Compatibility.** The iPad announces platform 3 (protocol 1.2). Osplay 0.1 refuses unknown
  platforms, so a computer whose Bonjour record has no `pm` key is listed as needing an update.
- **USB cable** (PROTOCOL.md "Stream links"). Only the computer can open a connection over the
  cable, through usbmuxd. Tapping **Computer on the USB cable** makes the iPad listen on loopback
  port 41847; a sharing Mac or PC follows attached iPads with the core's `UsbWatcher` and tries each
  every 2 s (`ConnectionViewModel` on both), so it connects within seconds. Those tries fail quietly
  while the iPad is not waiting. Control and media share the one stream; neither side reconnects by
  itself after a disconnect (DECISIONS.md, 2026-10-27).

## Android app

Also a viewer only. The C++ core is compiled by the NDK into `libosplay_jni.so` together with a thin
JNI bridge (`osplay_jni.cpp`): text crosses as UTF-8 byte arrays, the core's I/O thread attaches to
the JVM once and its callbacks go to a Kotlin `SessionListener`.

- **Discovery** (`discovery/NsdDiscovery.kt`). NsdManager browses `_osplay._tcp`; before Android 14
  it resolves one service at a time, so resolves wait in a queue. TXT records are checked by the
  core, as everywhere else.
- **Decode and present** (`video/`, `viewer/StreamStage.kt`). MediaCodec in low-latency, realtime
  mode decodes onto a SurfaceView and renders every frame at once. The stage starts the decoder
  when both the surface and the stream exist; after a codec error it starts over at the next
  keyframe and asks the host for one. Windows hosts' separate cursor is drawn above the video.
- **Keys** (`storage/`). The identity's secret key is encrypted with an AES key that stays in the
  Android Keystore; paired computers are JSON in app-private preferences.
- **Lifecycle.** The activity handles rotation itself, so the session restarts with the new shape
  instead of the activity being recreated; `onStop` ends the session cleanly and `onStart`
  reconnects. The screen stays on and the system bars are hidden while viewing.

## Discovery and pairing

A host advertises `_osplay._tcp` with its core's ephemeral control port and the TXT record from
`core` (`osplay_advertisement_to_txt`: protocol version, name, device ID, roles, platform); the
instance name is the device ID, so a rename never looks like a new device. Viewers browse, resolve
and connect to the advertised host name and port. A core host session takes one viewer, so the host
starts the next session, on the same port, as soon as a viewer introduces itself; the advertisement
stays as long as sharing is on and changes only when the port does.

- **macOS** (`Services/Discovery`, `Services/KeyStore`): the system DNS-SD API (`DNSServiceRegister`,
  `DNSServiceBrowse`, `DNSServiceResolve`) on one serial queue; the identity's secret and the paired
  devices (JSON) are generic-password Keychain items. `PeerTrustCache` answers the core's trust
  query from memory. `Features/Connection` is the temporary Devices section; `PairingPanel` shows the
  code in a floating panel.
- **Windows** (`engine/src/discovery`, `engine/src/keystore`): `DnsServiceRegister`/`Browse`/
  `Resolve` from `windns.h`; `identity.bin` and `peers.bin` in `%APPDATA%\Osplay`, each encrypted
  with DPAPI for the user. Host and viewer sessions started without a secret key use this identity,
  answer trust from the paired devices and store a pairing themselves, so the secret never reaches
  the shell. The WPF tray (`Osplay.App/Connection`, `Engine`, `Interop`) calls this through
  `LibraryImport` and function pointers and marshals every callback to the dispatcher.

Trust: a known key connects without a code, an unknown key shows the six-digit code on both sides,
and a revoked key is refused (`NOT_TRUSTED`) until it is removed from the paired devices. Revoking
or removing a device ends a session already running with it; on Windows every session using the
stored identity is registered (`capi/session_trust.hpp`), so a change also refreshes the copy a
listening host answers from.

## Virtual Display Driver control surface (Windows)

Findings from reading the driver source (VirtualDrivers/Virtual-Display-Driver, `master` at
`d7244969`, file `Virtual Display Driver (HDR)/MttVDD/Driver.cpp`), Phase 2:

| Aspect | Behavior |
|--------|----------|
| Device | Root-enumerated, hardware ID `Root\MttVDD` (`MttVDD.inf`), UMDF IddCx driver. |
| Settings file | `vdd_settings.xml` in the directory named by `HKLM\SOFTWARE\MikeTheTech\VirtualDisplayDriver\VDDPATH`, default `C:\VirtualDisplayDriver`. Elements used: `monitors/count`, `gpu/friendlyname` (DXGI adapter description or `default`), `resolutions/resolution/{width,height,refresh_rate}`, `global/g_refresh_rate` (multiplies every resolution). |
| When settings are read | Only in `EvtDriverDeviceAdd` (`loadSettings()`), i.e. when the device starts. |
| Preferred mode | The first resolution in the file (`PreferredMonitorModeIdx = 0`). |
| Monitor count | `count = 0` is coerced to 1. A monitor cannot be removed by count. Two or more monitors on one device make the whole Windows desktop stutter (upstream issues #140, #319, #449; fixed by PR #482, which no signed release contains yet). |
| Several devices | Every `Root\MttVDD` device runs the same driver and reads the same settings file. |
| Driver host processes | UMDF pools a driver's devices into one WUDFHost process by default, so they share the driver's globals. This driver appends each monitor's modes to a global list it never clears (`s_KnownMonitorModes2`) and reports its first entries: a device started after another offers the earlier device's modes, and a new monitor cannot take a mode only it needs. Osplay gives every device a process of its own: `Device Parameters\WUDF\HostProcessSharingEnabled` = 0 (what `UmdfHostProcessSharing = ProcessSharingDisabled` writes) and a `DeviceGroupId` of its own, `Osplay.` plus the instance ID, because the INF puts every device in `MttVDDGroup` and devices of one group share a process regardless. Both are read when the device starts, so devices the helper adds in one request share a process until they next start (issue #30). |
| Named pipe | `\\.\pipe\MTTVirtualDisplayPipe`, plain-text commands (`RELOAD_DRIVER`, `SETDISPLAYCOUNT n`, `SETGPU "name"`, `PING`, `GETSETTINGS`, logging and colour toggles). Its ACL is `D:(A;;GA;;;WD)` — any local user. |
| `RELOAD_DRIVER` / `SETDISPLAYCOUNT` | Call `InitAdapter()` without re-reading settings; new counts or modes in the file are not applied until the device restarts. |

Consequences for Osplay:

- **One device per monitor** (DECISIONS.md, 2026-10-25). The helper adds devices from the
  installed driver package when a session needs more than exist and keeps the spare ones disabled.
- Every configuration change is **write the file, then (re)start the devices in use**; ending a
  session is **disable the devices**, which removes the monitors. Both need administrator rights,
  hence the helper service.
- Osplay never uses the driver's pipe: it cannot change modes, and it is open to every user.
- Osplay owns `vdd_settings.xml` and rewrites it before every session: one monitor per viewer
  monitor (up to four), every distinct mode they need (the first monitor's first, which is the mode
  every monitor starts in), `count` 1, no global refresh rates, hardware cursor on (the pointer
  stays out of captured frames and is sent separately). Each device's monitor offers every listed
  mode; the engine finds the monitors device by device in instance ID order and sets each one's own
  mode.
- Osplay takes over the driver: other software using the same VDD installation will see its
  monitors disabled outside Osplay sessions.

## OsplayHelper service (Windows)

`windows/helper/` is a LocalSystem service. It is the only Osplay component with administrator
rights and accepts four commands from the engine:

| Command | Effect |
|---------|--------|
| Configure (1–4 monitors: width, height, refresh; one GPU name) | Validates the values and writes `vdd_settings.xml` (every mode in use). Running monitors keep theirs: no device restarts. |
| Enable (device mask) | Adds the named devices that do not exist yet, then enables them: their monitors appear. The other devices are untouched. Requires a prior Configure. |
| Disable (device mask) | Disables the named devices: their monitors disappear. Persistent across reboots. |
| Status | Reports whether the driver is installed, which devices are enabled, how many monitors are configured and the first one's mode. |

A device keeps its index while the service runs: known devices keep their places and new ones
are added at the end, even when a new device's instance ID fills a gap left by a removed one.

Safety rules:

- **One client at a time.** A second engine that connects while a session is active waits up to
  2 s and then gets `OSPLAY_ENGINE_ERROR_HELPER_BUSY`.
- **The monitors exist only while the client is connected.** The pipe accepts one client; when it
  disconnects for any reason (session end, engine crash), the console user logs off or switches,
  or the service stops, the helper disables the devices. On service start it disables them as
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
Request:  magic u32 = "OSPH" | version u16 = 3 | command u16 | payload_length u32 | payload
Response: magic u32 = "OSPH" | version u16 = 3 | result u16  | payload_length u32 | payload

Configure (command 1) payload:
  count u8 (1–4) | reserved u8 | gpu_name_length u16 | gpu_name (UTF-8) |
  count × (width u32 | height u32 | refresh_millihertz u32)
Enable (2), Disable (3) payload:
  devices u8   (bit i: driver device i; 1–15)
Status (4): no payload

Response payload (always 16 bytes):
  vdd_installed u8 | enabled_devices u8 (bit i: device i) | configured u8 | monitor_count u8 |
  width u32 | height u32 | refresh_millihertz u32   (the first monitor's mode)

Versions 1 (one monitor) and 2 (one device for every monitor) are refused with UnsupportedVersion:
the engine and the helper ship together.

Result codes: 0 Ok, 1 MalformedRequest, 2 UnsupportedVersion, 3 UnknownCommand,
  4 ValueOutOfRange, 5 VddNotInstalled, 6 DeviceControlFailed, 7 ConfigWriteFailed, 8 NotConfigured
```

### Engine API

`windows/engine/include/osplay/engine.h` exposes the virtual display as an opaque handle:
`osplay_virtual_display_open` (connects to the helper), `_create_monitors`, `_remove_monitor`,
`_status`, `_close`. Inside the engine, `VirtualDisplayProvider` is one host session's view of the
monitors. The helper serves one client, so every host session of the process shares one
`VirtualDisplayHub` over one `HelperChannel` (the named pipe in production, a fake in tests); it
closes when the last session goes. The hub gives each session (`HubVirtualDisplays`) free driver
devices, and a session's monitors come and go without restarting another session's.

Creating a session's monitors is three steps: the helper writes the settings with every mode in use
(Configure) and enables the session's devices (Enable with their mask); then the engine sets the mode
of each monitor that was not on the desktop before. Windows gives a new monitor its own default mode
(800×600 for this driver, measured on the developer PC) even when the driver offers only the
requested one, and display settings belong to the user's session, which a session-0 service cannot
change. `GdiDisplayModeApplier` therefore finds the monitors — the active CCD paths whose adapter
interface path starts with a driver device's instance ID (`ROOT\DISPLAY\0000` →
`\\?\ROOT#DISPLAY#0000#`) — waits up to 5 s for the new ones to join the desktop, stores each mode
with `ChangeDisplaySettingsExW` (`CDS_NORESET`), applies them together and reads every one back. If
that fails, the new monitors are removed again. The host then captures each monitor by its GDI name
with its own Desktop Duplication and encoder pipeline (one stream per viewer monitor); when the
viewer stops one stream, that session's other monitors are made again. A viewer that arrives when
no device is free is turned away as busy (`osplay_session_refuse_busy`) before it pairs.

## Host media pipeline on Windows

`osplay_engine_host_start` (`windows/engine/src/capi/host_capi.cpp`) runs a host session: the core
session, the virtual monitor through the helper, and one `StreamPipeline`. `HostOrchestrator`
(`src/host/`, portable and unit tested on every platform) mirrors macOS's `HostSession`: when the
viewer offers its monitors it adds the virtual monitor in the first one's mode, starts the
pipeline, configures one stream (separate cursor mode) and feeds access units and the cursor to the
core. Core callbacks are handled on its worker thread, so the core's I/O thread never waits for the
helper or the encoder.

```
VDD monitor ─► Desktop Duplication ─► BGRA copy ─► video processor (NV12) ─► async H.264 MFT ─► core
              └► pointer position/shape ─► BGRA cursor ─► cursor packets ─────────────────────────┘
```

- **Device.** The DXGI output whose GDI name matches the VDD monitor (found through the CCD API,
  `vdd_monitor.cpp`) decides the adapter; one D3D11 device on it (video support, multithread
  protected) serves duplication, conversion and encoding.
- **Capture** (`src/capture/`). One thread owns everything. `AcquireNextFrame` returns only when
  the screen changes; each new frame is copied to a BGRA texture (released at once, kept for
  keyframe re-sends). `DXGI_ERROR_ACCESS_LOST` (mode change, UAC, lock screen, full-screen apps)
  recreates the duplication every 100 ms until it succeeds; a vanished output is looked up again on
  the same adapter. Device removal ends the session.
- **Convert and encode** (`src/encode/`). The D3D11 video processor converts BGRA to NV12 at the
  stream size, BT.709 studio range. The virtual monitor keeps the viewer monitor's mode while the
  stream is fitted inside the shell's stream size setting (`max_stream_width/height` in the host
  config, `osplay_fit_stream_size`), so the processor scales it down. The first
  hardware H.264 MFT from `MFTEnumEx` runs in async mode with a DXGI device manager, low-latency
  mode, CBR, no B-frames and an endless GOP; `CODECAPI_AVEncVideoForceKeyFrame` answers keyframe
  requests. Output keyframes get SPS/PPS put in front when the encoder omits them
  (`media/h264_stream.cpp`).
- **Cursor.** Desktop Duplication reports the pointer apart from the image. Shapes (monochrome,
  colour, masked colour) become premultiplied BGRA (`media/pointer_shape.cpp`; screen-inverting
  pixels become black with a white outline, larger than 256 px are cropped around the hotspot),
  scaled down with the stream when it is smaller than the monitor, and positions are sent as the
  hotspot in stream pixels.
- **Clocks.** Capture times come from `LastPresentTime` (QPC) and are converted to the core's clock
  by their age.
- **Debug tool.** `OsplayHostTest.exe` runs a host session with an in-process core viewer, a moving
  pattern on the virtual monitor and a per-second report (BUILDING.md).

## Viewer pipeline on Windows

`osplay_engine_viewer_start` (`windows/engine/src/capi/viewer_capi.cpp`) runs a viewer session:
the core session, a UI thread and one `ViewerStream` per stream. `ViewerOrchestrator`
(`src/viewer/`, portable and unit tested) mirrors macOS's `ViewerSession`: each stream is shown on
the monitor the host chose, decoder trouble asks for a keyframe, hiding pauses every stream on the
host, and a monitor that disappears stops only its own stream (StreamStop to the host).

```
core ─► access unit ─► decode thread: MF H.264 decoder (D3D11, NV12) ─► video processor (BGRA,
        aspect fit) ─► flip-model swap chain ─► borderless window covering the monitor
```

- **Monitors** (`src/viewer/windows_monitors.cpp`, `monitor_layout.cpp`). Every active monitor
  except the Virtual Display Driver's: current mode from GDI, exact refresh rate and EDID name from
  the CCD API, physical size from `GetDeviceCaps`, scale from the effective DPI, all in physical
  pixels. Ids follow the desktop position, left to right; after a display change the offered
  monitors are matched by GDI name, so renumbering never stops the wrong stream.
- **UI thread** (`src/present/presentation_thread.cpp`). Per-monitor DPI aware; owns the windows,
  the global hide/show hotkey (the shell's choice, Ctrl+Alt+Shift+V by default, `RegisterHotKey`), `WM_DISPLAYCHANGE` and the
  display-required power state. Windows are topmost tool windows without a redirection bitmap and
  hide the local pointer.
- **Decode and present** (`src/decode/`, `src/viewer/windows_viewer_stream.cpp`). One thread per
  stream owns a D3D11 device on the adapter driving its monitor, the Media Foundation H.264 decoder
  (DXVA, low latency) and a `FLIP_DISCARD` swap chain with a frame latency waitable object and
  maximum latency 1. Each decoded frame is converted by the video processor (BT.709 studio range to
  full-range RGB, centred with black bars when the aspect ratio differs) and presented at once with
  `Present(0, 0)`. A window that covers the monitor with nothing drawn over it lets DWM switch to
  independent flip. A decode error, or a queue longer than 30 access units, drops frames until the
  next keyframe and asks for one (at most every 100 ms).
- **Stats overlay.** The shell formats one localized line from the session statistics and passes it
  with `osplay_engine_viewer_set_overlay`; the stream thread draws it with Direct2D and DirectWrite
  in the top-left corner, sized by the monitor's scaling. While the overlay shows, the picture is
  converted into a kept texture first, so a new line is drawn at once even when no frame arrives;
  without it frames go straight into the swap chain as before.
- **Debug tool.** `OsplayViewerTest.exe` lists the monitors, views a remote host by address or runs
  a loopback with the Windows host pipeline (BUILDING.md).

## Windows app UI

A tray app (`windows/app/Osplay.App`, WPF with WPF-UI's Fluent controls) shaped like the Mac's menu
bar app. Every visual value comes from `Theme/Tokens.xaml` and WPF-UI's theme brushes (referenced
with `DynamicResource`), every string from `Osplay.Localization` (English and Turkish);
`scripts/check_ui_tokens.sh` rejects literal sizes, colours and text in XAML.

- **Tray icon** (`Tray/`). A click opens the panel; the menu has status, the two roles, hide/show,
  Disconnect, Settings and Quit. The icon follows the taskbar's light or dark theme.
- **Panel** (`TrayPanel/`). A borderless Fluent window above the taskbar that closes when it loses
  focus: problems first (errors, helper or driver missing, update available), the
  role cards "Extend this PC" and "Use as display", the quality preset while sharing, and while
  viewing a picture of this PC's monitors (click one to leave it out) and the nearby computers.
- **The PC's own network** (`Services/PcNetwork.cs`, `Connection/PcNetworkViewModel.cs`). The
  panel's "No router?" card opens a WPA2 network with the WinRT Wi-Fi Direct publisher in legacy
  mode: the PC becomes its group owner and any device joins it as an ordinary Wi-Fi network
  (192.168.137.0/24, no internet through it), after which discovery and sessions work as on a
  router. The name (`Osplay-` and four characters) and password are generated once and kept in
  `settings.json`, so devices rejoin; the card shows them and the standard Wi-Fi QR code phones and
  tablets join from. Mobile hotspot takes the Wi-Fi card over and stops it (DECISIONS.md,
  2026-10-27). On Android, `WifiPreference` binds Osplay's sockets to such a Wi-Fi network while
  mobile data is the default route.
- **Settings** (`Settings/`). General (start with Windows through the Run key, stats overlay,
  recordable hide/show shortcut, daily update check against GitHub Releases), Devices (revoke,
  remove), Advanced (helper and driver health with repair guidance, network profile, logs folder).
  `AppSettings` keeps the values in `%APPDATA%\Osplay\settings.json`.
- **Engine calls** (`Engine/`). Every call into `OsplayEngine.dll` runs on the thread pool through
  one serial queue (`Native`), so the UI thread never waits for native code and calls on a handle
  never overlap; engine callbacks are marshalled to the dispatcher. A missing or damaged engine is
  detected at start and disables the roles with a message.
- **Logs** (`Diagnostics/Log.cs`). Engine and core log lines go to
  `%LOCALAPPDATA%\Osplay\logs\osplay.log` from a background writer, rolled over at 5 MB into
  `osplay.1.log` (the previous one is deleted), so the logs never take more than 10 MB.
- **Theme.** WPF-UI applies the system theme at start, and `SystemThemeWatcher` switches it live.

## Role swap

One shortcut (⌃⌥⌘S on macOS, Ctrl+Alt+Shift+S on Windows, both recordable) or the "Swap roles"
button reverses a running session: the computer used as a display becomes the host and the host
becomes its display (protocol: RoleSwap, PROTOCOL.md "Role swap").

```
A presses the shortcut ─► RoleSwap(request) ─► B checks it can take its new role
                                               (Mac host: Screen Recording; Windows host: helper and
                                               driver; either viewer: a monitor to offer)
A ◄─ RoleSwap(accept) + Bye(5) ◄───────────── B, or RoleSwap(refuse) and the session goes on
both sessions close ─► the new host advertises ─► the new viewer browses and connects to the
                                                  advertisement whose id is the peer's device ID
```

- The core reports the peer's static key with the request and the answer; the Windows engine turns
  it into the device ID the shell compares with Bonjour advertisements.
- Each app keeps a pending swap until its session has closed, then switches modes the usual way
  (Sharing or Viewing); a viewer that does not find the peer within 20 s says so and keeps browsing.
- Both keys are already trusted, so no pairing code appears. The swap shortcut is registered only
  while frames flow; if another program owns it, the button still works.

## Interruptions and recovery

Everyday disruptions end a session cleanly and the viewer brings it back; the host simply listens
again after every session (DEVPLAN Phase 13, test matrix in TESTING.md).

| Disruption | What happens |
|------------|--------------|
| This computer goes to sleep | It closes with Bye 3 first (`osplay_session_close_for_sleep`); a viewer resumes after wake |
| A Windows PC turns its display off (Modern Standby sends no suspend and keeps the network up) | Treated as sleep: Bye 3; when the display comes back on, sharing listens again |
| The other computer goes to sleep | The session closes with PEER_SLEEPING; a viewer waits for that host and reconnects when it advertises again |
| Network outage under 10 s | The session survives (heartbeat timeout 10 s); the stream freezes and resumes |
| Longer outage or network change | The session times out; a viewer waits for the same host (by device ID) and reconnects |
| A viewer monitor changes mode | Windows: the engine compares the offered monitors on `WM_DISPLAYCHANGE` and closes the session; macOS: the app compares its offered display on a screen change. The viewer reconnects with the new mode, so the host makes matching displays |
| A viewer monitor is unplugged | Only its stream stops (StreamStop); the session continues |
| The user disconnects | No reconnect |

Waiting viewers say "Waiting for … to come back"; choosing another computer or role ends the wait.

**Bitrate adaptation.** The core adapts each host stream's encoder bitrate from the viewer's reports
(PROTOCOL.md "Bitrate adaptation") and calls `on_bitrate_target`; VideoToolbox and the Media
Foundation encoder change their average bitrate on the fly. The configured quality preset is the
ceiling.

**Diagnostics.** Settings › Advanced › **Copy diagnostics** puts versions, settings, state and the
newest log lines on the clipboard, with device names and IP addresses replaced. The core logs each
session state, each stream's configuration, every bitrate target and, every 10 s, one summary line
per stream (frame rate, bitrate, loss, RTT, and totals of dropped frames, resends, NACKs and
keyframe requests), so a report shows how a session went. The stats overlay
also has a shortcut (⌃⌥⌘I / Ctrl+Alt+Shift+I, recordable).

