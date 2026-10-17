# Osplay — Development Plan

## Overview

Osplay turns one computer's screen into a real, OS-level extended display for another computer on the same local network. No HDMI, no remote control, no cloud.

Two use cases drive every decision:

1. **Mac host → Windows viewer (primary):** A MacBook Air (M3) gains two extra displays. macOS sees them as real monitors (System Settings → Displays, arrangement, window dragging, Mission Control). Their content is shown full screen on the two physical monitors attached to a Windows PC.
2. **Windows host → Mac viewer (occasional):** Windows gains one extra display. Windows sees it as a real monitor (Display Settings, Win+P, arrangement). Its content is shown full screen on the MacBook's built-in screen.

This is **not** remote desktop. The viewer never sends keyboard or mouse input to the host. The host is controlled with its own input devices; the viewer is a dumb, low-latency "monitor".

The project is open source, distributed as a notarized macOS app and a Windows installer, with no backend services of any kind.

---

## Platform & Stack

### Repository layout (monorepo)

- `core/` — shared, cross-platform C++20 library: protocol codec, framing, packetization, reassembly, encryption, handshake, transport, session state machine. Exposes a flat C API. Used by both apps so the wire protocol has exactly one implementation.
- `macos/` — Swift app (menu bar), consumes `core` via SwiftPM.
- `windows/engine/` — C++20 native engine DLL: capture, encode, decode, presentation, discovery. Links `core`. Exposes a flat C API.
- `windows/helper/` — C++20 Windows service for privileged virtual-display driver operations.
- `windows/app/` — C# WPF shell (tray app, settings, pairing UI). Calls the engine DLL via P/Invoke.
- `windows/installer/` — Inno Setup script.
- `protocol/` — protocol spec and binary test fixtures shared by every test suite.
- `docs/` — architecture, protocol, building, troubleshooting, security model.
- `.github/` — CI, release workflows, issue/PR templates.

### macOS

- Target: macOS 14 Sonoma and later. Apple Silicon is the supported and tested architecture; Intel builds are produced but marked untested.
- Language: Swift 6 with strict concurrency checking enabled.
- UI: SwiftUI (`MenuBarExtra` with `.window` style, `Settings` scene, a small onboarding window). AppKit only where SwiftUI cannot do the job (viewer presentation windows, global hotkey).
- Architecture: MVVM with `@Observable` view models. Sessions are actors. Platform services sit behind protocols and are injected.
- Project generation: XcodeGen (`project.yml`) so `.xcodeproj` is never hand-edited or merged.
- Key dependencies:
  - Sparkle 2: auto-updates with EdDSA-signed appcast.
  - `core` as a local SwiftPM package (C++ target with C++20, public C header only).
- System frameworks: `CoreGraphics` (private `CGVirtualDisplay` API), ScreenCaptureKit, VideoToolbox, AVFoundation (`AVSampleBufferDisplayLayer`), Network.framework (Bonjour), CryptoKit is **not** used (crypto lives in `core`), Security (Keychain).

### Windows

- Target: Windows 10 22H2 and Windows 11, x64. A GPU with hardware H.264 encode and decode is required (NVIDIA, AMD and Intel all qualify).
- Engine and helper: C++20, CMake with presets, MSVC, `/W4 /WX`. COM via `Microsoft::WRL::ComPtr`.
- Shell: C# on .NET 10 (LTS), WPF, nullable reference types enabled, warnings as errors.
- Architecture: native engine is layered behind interfaces with constructor injection. The C# shell uses MVVM (CommunityToolkit.Mvvm).
- Key dependencies:
  - Virtual Display Driver (VirtualDrivers/Virtual-Display-Driver, "MttVDD"): signed Indirect Display Driver that provides the virtual monitor. Installed, configured and controlled by Osplay; never forked unless Phase 2 proves it necessary.
  - WPF-UI: Fluent-style controls and system theme following.
  - H.NotifyIcon.Wpf: tray icon.
  - CommunityToolkit.Mvvm: view models.
  - Inno Setup: installer.
- System APIs: DXGI Desktop Duplication, D3D11 and D3D11 Video Processor, Media Foundation (hardware H.264 encoder and decoder MFTs), DNS-SD APIs (`DnsServiceRegister` / `DnsServiceBrowse`), SetupAPI / CfgMgr32 (device enable and disable), DPAPI.

### Shared core

- C++20, CMake, also exposed to SwiftPM as a package target.
- Dependencies (vendored under `core/third_party/` at pinned versions, with their licenses):
  - Standalone Asio: TCP and UDP sockets, timers, one I/O thread.
  - Monocypher 4: X25519, ChaCha20-Poly1305 (IETF), BLAKE2b. Single-file, no build friction on either platform.
  - GoogleTest: tests only, fetched via CMake FetchContent.
- No platform headers in `core` except through Asio. No logging library: `core` logs through a callback the host app installs.

### Cross-cutting decisions

- **Localization: Active, English and Turkish from day one.** macOS uses `Localizable.xcstrings`; the WPF shell uses `.resx` resources. The native engine and `core` never produce user-facing text; they return stable error codes that the UI layers map to localized messages.
- **Theming: System-following light and dark on both platforms.** All colors, typography, spacing, radii and elevation come from one theme source per platform. macOS: a `Theme` namespace backed by asset-catalog colors with light/dark variants. Windows: `Theme.Light.xaml` and `Theme.Dark.xaml` resource dictionaries plus a shared `Tokens.xaml`; WPF-UI's system theme watcher switches between them.
- **License: MIT.** Before Phase 0 completes, confirm that the Virtual Display Driver's license permits redistribution of its signed binaries in the Osplay installer. If not, the installer downloads the official release (or uses `winget`) instead of bundling it.
- **Wire video codec: H.264, Annex B byte stream, no B-frames.** HEVC and 4:4:4 are post-MVP.

---

## Constraints & Platform Considerations

### macOS

- **`CGVirtualDisplay` is a private API.** Declare the needed interfaces (`CGVirtualDisplay`, `CGVirtualDisplayDescriptor`, `CGVirtualDisplaySettings`, `CGVirtualDisplayMode`) in a small Objective-C header inside the app target and isolate every use behind a `VirtualDisplayProviding` protocol. The display exists only while the object is retained; releasing it removes the display. Reference implementations to study for behavior (not to copy): DeskPad, FluffyDisplay, BetterDisplay's documented behavior.
- **Consequences of the private API:** no App Store, no App Sandbox. Distribute with Developer ID, Hardened Runtime and notarization (the user holds a Developer ID: `AKINALP FIDAN (WQ54PPL5VQ)`). Private API use does not block notarization.
- **Stable display identity:** give each virtual display a deterministic vendor ID, product ID and serial number derived from the peer's identity key and the remote monitor index. macOS stores arrangement per identity, so the user's arrangement survives reconnects.
- **HiDPI:** create the display with HiDPI enabled and offer modes so the user can pick "looks like" sizes. The capture resolution always equals the remote monitor's physical pixel size; ScreenCaptureKit scales.
- **M3 MacBook Air external display limit:** the hardware drives one external display with the lid open. Virtual displays are composited in software by WindowServer and are expected to bypass this limit. Phase 1 exists to prove this on the actual machine before anything else is built.
- **TCC permissions:**
  - Screen Recording is required for ScreenCaptureKit. Onboarding must detect missing permission, explain why, and deep-link to System Settings. macOS 15+ may periodically re-confirm screen recording permission; the UI must handle revocation mid-session without crashing and surface a clear message.
  - Local Network access (macOS 15+) is required for Bonjour and LAN sockets. Info.plist needs `NSLocalNetworkUsageDescription` and `NSBonjourServices` listing the Osplay service type.
- **Info.plist:** `LSUIElement = true` (menu bar only, no Dock icon).
- **Global hotkey** (toggle viewer, emergency exit from full screen): use Carbon `RegisterEventHotKey`, which does not require Accessibility permission. Do not use `NSEvent` global monitors.
- **Power:** hold an `IOPMAssertion` that prevents display sleep while acting as a viewer. As a host, follow normal Mac sleep behavior; on sleep, pause streams and tell the viewer.
- **App Nap:** disable for the process during active sessions via `ProcessInfo.beginActivity` with latency-critical options.

### Windows

- **Virtual Display Driver (VDD):** configured through `C:\VirtualDisplayDriver\vdd_settings.xml` (monitor count, resolutions, refresh rates, GPU selection) and exposes a named pipe used by its own control app. Phase 2 must read the VDD source to determine the exact control surface (pipe commands such as reload, how resolution lists are applied, whether monitor count can change without a device restart). Anything not controllable at runtime falls back to rewriting the XML and restarting the device through SetupAPI.
- **Privilege split:** writing VDD config and enabling/disabling the device require admin rights. These operations live in the `OsplayHelper` Windows service (LocalSystem). The user-level engine talks to it over a named pipe whose ACL allows only the interactive user's SID and SYSTEM. The helper accepts a small, validated command set and nothing else.
- **Virtual monitor lifecycle:** the virtual monitor must exist only while a session is active. Outside sessions the VDD device is disabled, so Windows never moves windows onto an invisible monitor.
- **Display scaling:** Windows cannot reliably set per-monitor DPI scaling programmatically through public API. Set the resolution to the MacBook panel's native pixel size; the UI recommends 200% scaling and links to Display Settings.
- **Desktop Duplication specifics:**
  - Duplicate the output on the adapter that owns the VDD monitor. Configure VDD to attach to the same GPU that runs the encoder.
  - Frames arrive only when content changes. The viewer must keep showing the last frame, and the host sends a heartbeat on the control channel.
  - `DXGI_ERROR_ACCESS_LOST` happens on mode changes, UAC prompts and lock screen. Recreate duplication automatically. Secure desktop capture is post-MVP; during it the viewer shows a localized "protected screen" placeholder.
  - Pointer shape and position come separately from frames. Send them as a separate cursor stream rather than compositing into video (lower latency, no re-encode on pointer movement).
- **Encoder:** enumerate hardware H.264 encoder MFTs with `MFTEnumEx` (hardware flag), use the asynchronous MFT event model, and feed D3D11 NV12 textures through a DXGI device manager. Convert BGRA to NV12 on the GPU with the D3D11 Video Processor. Enable low-latency mode, disable B-frames, use a very long GOP with on-demand IDR (`CODECAPI_AVEncVideoForceKeyFrame`).
- **Decoder and presentation:** hardware H.264 decoder MFT with D3D11 awareness and low-latency mode. Render decoded NV12 textures to a flip-model swap chain (`FLIP_DISCARD`, frame latency waitable object, max frame latency 1) in a borderless window that exactly covers the target monitor, so DWM can use independent flip. Present as soon as a frame is decoded.
- **Global hotkey:** `RegisterHotKey` for viewer toggle.
- **Power:** `SetThreadExecutionState` with display-required while viewer windows are visible.
- **Firewall:** the installer adds inbound rules for the engine executable on the Private profile only. If the active network profile is Public, the UI explains the problem and links to network settings instead of silently failing.
- **Code signing:** the user has no Windows certificate. Unsigned builds trigger SmartScreen. Plan to apply to SignPath Foundation (free signing for open-source projects; VDD uses the same route). Until then, document SmartScreen behavior in the README.

### Network

- Same LAN only, no relay, no internet requirement.
- Wired gigabit is the reference setup. 5 GHz Wi-Fi 5/6 is supported with documented expectations.
- Listener ports are ephemeral and advertised via Bonjour. No hardcoded ports anywhere; the firewall rule is per-executable.

---

## Architecture

### Roles and sessions

Every installation can act as **host** (owns virtual displays, captures, encodes, sends) or **viewer** (receives, decodes, presents full screen). One active role per device at a time in the MVP. One session connects one host to one viewer and carries one video stream per virtual display (Mac host: up to 2; Windows host: 1).

The viewer drives configuration. It advertises its physical monitors (pixel size, refresh rate, physical size in mm, backing scale), the user picks which monitors to lend, and the host creates matching virtual displays.

### Data flow

```
HOST                                                         VIEWER
Virtual display ─► Capture ─► Encoder ─► core: packetize ──UDP──► core: reassemble ─► Decoder ─► Presenter
 (CGVirtualDisplay   (SCK /      (VT /      + encrypt               + decrypt          (VT / MF)  (AVSBDL /
  / VDD)              DXGI DD)    MF)                                                             D3D11 swapchain)
                                              ▲                        │
                                              └──── TCP control ◄──────┘
                                    (hello, config, IDR request, NACK, stats, heartbeat)
Cursor (Windows host only) ─► core ──UDP cursor packets──► Presenter overlay
```

### Layers

**core (C++, shared)**
- `protocol` — message and packet definitions, encode/decode, version negotiation. Pure functions, no I/O.
- `crypto` — Noise handshake, transport cipher states, replay window. Pure, deterministic, tested with published vectors.
- `transport` — Asio-based TCP control channel and UDP media channel, one I/O thread owned by `core`.
- `media` — packetizer (frame → MTU-sized fragments), reassembler (fragments → frames with deadline), loss detection, NACK bookkeeping.
- `session` — state machine (Idle → Connecting → Handshaking → Pairing? → Negotiating → Streaming ⇄ Paused → Closed), heartbeat, clock offset estimation, stats aggregation.
- `capi` — flat C API: opaque handles, POD structs, callbacks with a context pointer, error codes. The only public header.

`core` knows nothing about video APIs, displays or UI. Platforms push encoded access units in and receive reassembled access units out.

**macOS app**
- `App` — SwiftUI scenes, menu bar UI, settings, onboarding.
- `Features/Host` — `HostSession` actor: owns virtual displays, capture streams, encoders, and the core session.
- `Features/Viewer` — `ViewerSession` actor: owns the core session, decoders, presentation windows.
- `Features/Pairing` — pairing flow and trusted device management.
- `Services` — protocol-backed adapters: `VirtualDisplayProviding`, `ScreenCapturing`, `VideoEncoding`, `VideoDecoding`, `FramePresenting`, `PeerDiscovering`, `KeyStoring`, `PermissionChecking`, `HotkeyRegistering`, `PowerAssertionManaging`.
- `CoreBridge` — Swift wrapper over the C API that converts callbacks into async sequences and maps error codes to Swift error enums.
- `Theme`, `Localization`, `Logging` (os `Logger` with one subsystem and per-module categories).

**Windows engine (C++ DLL)**
- `capture` — Desktop Duplication per output, pointer shape/position extraction.
- `encode` — MF hardware encoder wrapper, BGRA→NV12 GPU conversion.
- `decode` — MF hardware decoder wrapper.
- `present` — per-monitor presentation windows on a dedicated UI thread, swap chains, cursor overlay.
- `display` — `IVirtualDisplayProvider` implemented by a client of the helper service.
- `discovery` — DNS-SD register/browse.
- `keystore` — DPAPI-protected identity key and trusted peers file under `%APPDATA%\Osplay`.
- `session` — host and viewer session orchestration on top of `core`.
- `capi` — flat C API consumed by C#.

**Windows helper service (C++)**
- Named pipe server with a strict command set: configure virtual monitors (count, modes, GPU), enable device, disable device, report status. Validates every argument against bounds; rejects everything else.

**Windows shell (C#)**
- `Interop` — `LibraryImport` source-generated P/Invoke, callbacks via `UnmanagedCallersOnly` function pointers, marshaling to the UI dispatcher.
- `ViewModels`, `Views`, `Services` (settings persistence, update check against GitHub Releases), `Theme`, `Resources` (`.resx`).

### State and persistence

- Identity key pair (X25519 static): macOS Keychain; Windows DPAPI-protected file.
- Trusted peers (public key, display name, last seen): same stores as the identity.
- User settings (quality preset, hotkeys, monitor selections per peer, HiDPI choice): `UserDefaults` on macOS; JSON file in `%APPDATA%\Osplay` on Windows.
- Session state lives only in memory inside the session actor/object.

---

## Protocol Requirements

Phase 3 writes the authoritative `docs/PROTOCOL.md` and binary fixtures in `protocol/fixtures/`. These decisions are fixed:

### General
- All integers little-endian. Every message and packet starts with a 2-byte magic and a 1-byte protocol version.
- Version negotiation happens in the first control message; incompatible major versions fail with a specific error code the UI can explain ("update the other device").

### Discovery
- Bonjour service type `_osplay._tcp`. TXT record: protocol version, device name, device ID (fingerprint of the identity public key), available roles, platform.

### Control channel (TCP)
- Framing: 4-byte length prefix, then the encrypted payload (after handshake).
- Message types: Hello, HandshakeMessage (×3, Noise XX), PairingConfirm, Capabilities, ViewerMonitors (list of monitors the viewer offers), StreamConfig (per stream: id, width, height, refresh, codec, profile, bitrate, cursor mode), StreamStart, StreamPause, StreamResume, StreamStop, KeyframeRequest (stream id, reason), Nack (stream id, frame id, missing fragment ranges), ReceiverReport (per stream: frames received/dropped, loss %, decode time, jitter), Ping/Pong (for RTT and clock offset), Heartbeat, Error (code), Bye.
- Payload encoding: hand-written fixed binary layouts documented field by field in `PROTOCOL.md`. No protobuf or JSON on the wire.

### Media channel (UDP)
- The viewer opens the UDP association after the handshake by sending a UDP Hello containing a session token issued over the control channel. The host learns the viewer's endpoint from it.
- Video packet header: magic, version, packet type, stream id, frame id, fragment index, fragment count, flags (keyframe, codec config present), capture timestamp in host clock microseconds, packet sequence number.
- Payload: one fragment of an Annex B access unit. Fragment payload size sized so the full UDP datagram stays at or below 1200 bytes.
- Every IDR access unit carries SPS and PPS in-band.
- Cursor packets (Windows host only): position updates (latest wins, sent at most at display refresh rate) and shape updates (BGRA image, hotspot, shape id; shape sent on change, re-sent with each IDR so late viewers recover).
- Every UDP packet is AEAD-encrypted with the media transport key; the nonce is derived from the packet sequence number; the receiver keeps a sliding replay window.

### Security model
- Handshake: Noise `XX_25519_ChaChaPoly_BLAKE2b`, implemented in `core` with Monocypher primitives and validated against published Noise test vectors.
- First connection between two devices: after the handshake, both UIs show a 6-digit code derived from the handshake hash. The user confirms on both devices; only then is the peer stored as trusted. Without confirmation, no stream starts.
- Later connections: the peer's static key must match a trusted entry or the connection is refused with a specific error.
- Media keys derived from the Noise split, separate keys per direction and per channel.
- No unencrypted mode exists in release builds. A debug-only loopback mode may skip pairing (never encryption) and is compiled out of release configurations.

### Media behavior
- Host encoders: realtime, no frame reordering, very long GOP, IDR only on request or stream start, capped bitrate from the quality preset, low-latency rate control where available.
- Viewer: no jitter buffer. Decode and present immediately. If a frame is incomplete at its deadline (default one frame interval plus measured RTT), drop it, send a NACK once if time allows, otherwise request an IDR. IDR requests are rate-limited per stream.
- Quality presets: Balanced, High, Ultra. Each maps to bits-per-pixel-per-second, so bitrate scales with resolution and refresh. Defaults live in one constants file per platform.
- Simple bitrate adaptation from ReceiverReports: step down on sustained loss, step up slowly when clean, bounded by the preset.

---

## Feature Scope

### MVP
- Mac host: up to two virtual displays, matched to the Windows viewer's selected monitors, with HiDPI "looks like" choice and persistent arrangement.
- Windows viewer: full-screen presentation on one or both selected monitors, hotkey to hide/show, disconnect.
- Windows host: one virtual display matched to the MacBook's built-in screen.
- Mac viewer: full-screen presentation on the built-in screen with Windows cursor overlay, hotkey to exit.
- Automatic discovery on the LAN.
- Pairing with code confirmation, trusted device list, revoke.
- End-to-end encryption on both channels.
- Quality presets and stats overlay (fps, bitrate, RTT, loss, end-to-end latency estimate).
- Reconnect after brief network loss; clean handling of sleep/wake and display reconfiguration.
- English and Turkish UI, light and dark themes.
- Notarized macOS DMG with Sparkle updates; Windows installer that installs the app, helper service, VDD and firewall rules.
- Complete open-source repository hygiene: README, docs, license, contributing guide, security policy, CI.

### Post-MVP
- HEVC and 4:4:4 chroma for sharper colored text where hardware supports it (verify Apple Silicon VideoToolbox decode support and NVENC/AMF encode support first).
- Forward error correction for lossy Wi-Fi.
- Direct NVENC / AMF / QuickSync encoder paths on Windows for lower latency than the MF wrapper.
- High refresh (120 Hz) streams.
- Secure desktop (UAC, lock screen) capture on Windows via a session-0 capture helper.
- Both roles active at the same time on one device.
- Windows host with two virtual displays and multiple viewers.
- Homebrew cask and winget manifest for Osplay itself.
- Optional input forwarding is explicitly **out of scope**; the project is not a remote desktop tool.

---

## Phases

Each phase is meant to be completed in one Claude Code session. A phase is done only when every acceptance criterion is checked and the build and tests pass on CI.

### Phase 0: Repository scaffolding
**Goal**: A clean, empty-but-building monorepo with all hygiene and tooling in place.
**Delivers**:
- Directory layout as described in Platform & Stack.
- `README.md` skeleton (sections listed in Phase 14), `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` (private vulnerability reporting via GitHub Security Advisories), `CODE_OF_CONDUCT.md`, issue and PR templates.
- `.gitignore` covering Xcode, SwiftPM, CMake build dirs, Visual Studio, .NET, Inno output; `.gitattributes` with line-ending rules; `.editorconfig`.
- Formatting and linting: `.clang-format`, `.clang-tidy`, swift-format config, .NET analyzers with `Directory.Build.props` enabling nullable and warnings-as-errors.
- `core/` CMake project with an empty library and one passing test; `Package.swift` exposing `core` to SwiftPM.
- `macos/project.yml` generating an empty menu bar app that links `core` and launches.
- `windows/` CMake presets for engine and helper (empty targets), a WPF shell solution that launches to a tray icon.
- GitHub Actions: macOS job (build core + app, run tests), Windows job (build core, engine, helper, shell, run tests).
- `docs/` with placeholder `ARCHITECTURE.md`, `PROTOCOL.md`, `BUILDING.md`, `TROUBLESHOOTING.md`, `SECURITY_MODEL.md`.
- Third-party license review note in `docs/THIRD_PARTY.md`, including the VDD redistribution decision.
**Depends on**: Nothing
**Acceptance criteria**:
- [ ] Fresh clone builds on macOS (terminal) and Windows (command line) by following `BUILDING.md` exactly.
- [ ] Both CI jobs are green.
- [ ] Formatters and linters run in CI and fail on violations.
- [ ] VDD license compatibility recorded with the chosen distribution approach.

### Phase 1: macOS virtual displays (go/no-go gate)
**Goal**: The Mac app can create and remove up to two real virtual displays on the M3 MacBook Air.
**Delivers**:
- Private API declarations isolated in one Objective-C header and one Swift wrapper.
- `VirtualDisplayProviding` protocol and its `CGVirtualDisplay` implementation: create with given pixel size, refresh rate, physical size, HiDPI flag and deterministic identity; update modes; destroy.
- Temporary developer menu (debug builds only) to create/remove 1 or 2 test displays at chosen resolutions.
**Depends on**: Phase 0
**Acceptance criteria**:
- [ ] With the lid open on the M3 Air, two virtual displays appear simultaneously in System Settings → Displays.
- [ ] Windows can be dragged onto both; Mission Control treats them as separate displays.
- [ ] HiDPI "looks like" modes are selectable.
- [ ] Removing and recreating with the same identity restores the previous arrangement.
- [ ] Destroying the provider removes the displays and windows move back to the built-in screen.
- [ ] **If two simultaneous displays cannot be created, stop and document findings before any further phase.**

### Phase 2: Windows virtual display through VDD and the helper service
**Goal**: The Windows engine can create and remove one virtual monitor with an exact custom resolution, without admin prompts during normal use.
**Delivers**:
- Written findings in `docs/ARCHITECTURE.md` on VDD's control surface (config schema used, pipe commands, what needs a device restart).
- `OsplayHelper` service: install/uninstall via command-line flags, named pipe with restrictive ACL, validated command set (configure, enable, disable, status).
- Engine `IVirtualDisplayProvider` implemented as a helper client.
- Temporary developer console tool to drive it.
**Depends on**: Phase 0
**Acceptance criteria**:
- [ ] With VDD installed, the tool creates one monitor at 2560×1664 at 60 Hz, visible in Display Settings.
- [ ] The monitor disappears entirely when the session ends; no ghost monitor remains after reboot.
- [ ] A non-admin user process can drive the helper; another user's process cannot connect to the pipe.
- [ ] Invalid commands and out-of-range values are rejected and logged.

### Phase 3: Protocol codec and fixtures
**Goal**: The complete wire format exists as a pure, tested library and a written specification.
**Delivers**:
- `docs/PROTOCOL.md` documenting every message and packet field by field.
- `core/protocol` encode/decode for all control messages and media packets with strict bounds checking.
- Binary fixtures in `protocol/fixtures/` (valid and malformed cases).
- C++ tests and Swift tests (via `CoreBridge`) that both run against the same fixtures.
- Initial `capi` surface for version info and error codes.
**Depends on**: Phase 0
**Acceptance criteria**:
- [ ] Round-trip tests pass for every message type.
- [ ] Malformed fixtures are rejected with specific error codes, never crash; a fuzz target exists for the decoder.
- [ ] Swift and C++ suites pass on CI against identical fixtures.

### Phase 4: Crypto and handshake
**Goal**: Two `core` instances can mutually authenticate and derive transport keys.
**Delivers**:
- Monocypher vendored with license.
- Noise XX implementation, transport cipher states, replay window, pairing code derivation from the handshake hash.
- Tests against published Noise test vectors.
- `capi` functions for identity key generation and import/export (storage stays in platform code).
**Depends on**: Phase 3
**Acceptance criteria**:
- [ ] Published Noise XX vectors pass.
- [ ] Tampered, replayed and reordered-beyond-window messages are rejected.
- [ ] Both sides derive the same 6-digit pairing code; any change to the transcript changes it.

### Phase 5: Transport and session
**Goal**: Two `core` instances on the same machine or LAN can open an encrypted session and exchange control messages and media packets.
**Delivers**:
- Asio TCP listener/connector for control, UDP media socket, UDP association via session token.
- Packetizer and reassembler with deadlines, NACK bookkeeping, keyframe request rate limiting.
- Session state machine, heartbeat, ping/pong with RTT and clock offset estimation, stats aggregation.
- `capi` for creating host/viewer sessions, pushing access units (host), receiving access units (viewer), callbacks for state, stats and errors, and a log callback.
- Loopback integration test that streams synthetic access units through a simulated lossy link.
**Depends on**: Phase 4
**Acceptance criteria**:
- [ ] Loopback test reassembles 100% of frames on a clean link and recovers via NACK or IDR under 5% simulated loss.
- [ ] Session survives a 3-second network interruption and resumes without user action.
- [ ] No unencrypted bytes leave the process after the handshake (verified by a test that inspects the wire).
- [ ] Thread sanitizer and address sanitizer runs are clean on macOS CI.

### Phase 6: macOS host media pipeline
**Goal**: The Mac captures its virtual displays and produces encoded streams into `core`.
**Delivers**:
- `ScreenCapturing` with ScreenCaptureKit per virtual display: pixel size from the stream config, 420v pixel format straight into the encoder, cursor composited, idle frame handling.
- `VideoEncoding` with VideoToolbox: realtime, low-latency rate control, no reordering, forced IDR on request, AVCC → Annex B conversion with in-band parameter sets.
- `HostSession` actor wiring displays, capture, encoders and the core session.
- Debug option to dump a stream to an `.h264` file for inspection.
**Depends on**: Phase 1, Phase 5
**Acceptance criteria**:
- [ ] Two simultaneous 2560×1440 at 60 Hz streams run with stable frame pacing.
- [ ] Dumped files play correctly in a standard player.
- [ ] Revoking Screen Recording permission mid-session stops cleanly with a surfaced error.

### Phase 7: macOS viewer pipeline
**Goal**: The Mac can receive and display a stream full screen on its built-in screen with minimal latency.
**Delivers**:
- `VideoDecoding` with VideoToolbox: format description from in-band SPS/PPS, Annex B → AVCC, decode errors trigger keyframe requests.
- `FramePresenting` with `AVSampleBufferDisplayLayer` (display-immediately) in a borderless window covering the target screen, menu bar and Dock hidden, cursor hidden when idle.
- Cursor overlay layer for Windows-host cursor packets.
- Exit hotkey and power assertion.
- Debug-only loopback: the Mac hosts a virtual display and views it in a window on itself.
**Depends on**: Phase 6
**Acceptance criteria**:
- [ ] Loopback on the Mac shows the virtual display content smoothly at 60 fps.
- [ ] Hotkey always exits full screen, even when frames stop arriving.
- [ ] Decoder recovers from injected corruption via IDR within one second.

### Phase 8: Windows host media pipeline
**Goal**: Windows captures its virtual monitor and produces an encoded stream and cursor stream into `core`.
**Delivers**:
- Desktop Duplication capture on the VDD monitor's adapter with access-lost recovery.
- GPU BGRA→NV12 conversion and MF hardware H.264 encoder in async mode with low-latency settings and forced IDR.
- Pointer shape conversion (monochrome, color, masked color → BGRA) and cursor packets.
- Host session orchestration in the engine.
- Debug `.h264` dump.
**Depends on**: Phase 2, Phase 5
**Acceptance criteria**:
- [ ] Stable 60 fps stream at 2560×1664 on NVIDIA, AMD or Intel hardware (whichever the developer has; others recorded in a test matrix).
- [ ] Mode change, UAC prompt and lock/unlock do not crash the pipeline; streaming resumes afterwards.
- [ ] Cursor shapes (arrow, text beam, resize, custom app cursors) are transmitted correctly.

### Phase 9: Windows viewer pipeline
**Goal**: Windows can present up to two streams full screen on chosen physical monitors.
**Delivers**:
- MF hardware decoder with D3D11 output and low latency.
- Per-monitor borderless presentation windows with flip-model swap chains on a dedicated thread.
- Monitor enumeration (pixel size, refresh, physical size, DPI) exposed through the C API for the ViewerMonitors message.
- Hide/show hotkey, display-required power state.
- Debug loopback: Windows hosts a VDD monitor and views it on a physical monitor.
**Depends on**: Phase 8
**Acceptance criteria**:
- [ ] Two streams render simultaneously on two monitors with independent flip active (verified with PresentMon).
- [ ] Hide/show toggles instantly; hidden viewers tell the host to pause.
- [ ] Unplugging one monitor mid-session stops only that stream and informs the host.

### Phase 10: Cross-platform interop, discovery and pairing
**Goal**: Mac and Windows find each other, pair, and run both use cases end to end.
**Delivers**:
- Bonjour advertise/browse on macOS (Network.framework) and DNS-SD on Windows, feeding listener endpoints to `core`.
- Pairing flow surfaced through minimal temporary UI on both sides (final UI comes later).
- Keychain and DPAPI key stores.
- Viewer-driven negotiation: monitor offer, host creates matching virtual displays, stream configs, start.
**Depends on**: Phase 7, Phase 9
**Acceptance criteria**:
- [ ] Use case 1: the Mac gains two displays shown on the PC's two monitors.
- [ ] Use case 2: Windows gains one display shown on the MacBook's screen, with the Windows cursor visible.
- [ ] Unpaired devices cannot start streams; a revoked device is refused.
- [ ] Measured end-to-end latency on wired gigabit: median under 40 ms; on 5 GHz Wi-Fi: median under 60 ms. Method documented in `docs/TROUBLESHOOTING.md` (on-screen millisecond clock photographed together with the viewer).

### Phase 11: macOS app UI
**Goal**: A simple, clean, finished Mac interface.
**Delivers**:
- Menu bar popover: discovered devices, paired devices, role switch (Share my screens / Use as display), connect/disconnect, current session status and quality preset.
- Settings: General (launch at login via `SMAppService`, hotkeys), Displays (per-peer HiDPI choice), Devices (trusted list, revoke, rename), Advanced (stats overlay, logs folder).
- Onboarding: permissions (Screen Recording, Local Network) with live status and deep links.
- Pairing sheet showing the code with confirm/cancel.
- Complete English and Turkish strings, theme tokens for every visual value, full VoiceOver labels.
**Depends on**: Phase 10
**Acceptance criteria**:
- [ ] No hardcoded strings, colors, fonts or spacing in views (enforced by review checklist and a lint rule where possible).
- [ ] Switching the system language and appearance updates the UI correctly.
- [ ] Every error code from `core` and services maps to a localized, actionable message.

### Phase 12: Windows app UI
**Goal**: A simple, clean, finished Windows interface.
**Delivers**:
- Tray icon with menu: status, connect/disconnect, hide/show viewer, open settings, quit.
- Main window (Fluent, WPF-UI): devices, role switch, monitor selection for lending (with a visual layout of the PC's monitors), quality preset.
- Settings: start with Windows, hotkeys, trusted devices, stats overlay, logs folder, update check.
- Pairing dialog, network profile warning, VDD/helper health check with repair guidance.
- P/Invoke layer with callback marshaling to the dispatcher.
- Complete English and Turkish `.resx`, theme dictionaries following the system theme.
**Depends on**: Phase 10
**Acceptance criteria**:
- [ ] No hardcoded strings or visual values in XAML or code-behind.
- [ ] System light/dark switch is reflected live.
- [ ] Engine crashes or helper unavailability are surfaced clearly, and the shell never hangs on a native call (all native calls off the UI thread).

### Phase 13: Robustness and diagnostics
**Goal**: The system behaves predictably in everyday disruptions.
**Delivers**:
- Sleep/wake handling on both sides; network change handling; host display reconfiguration (resolution change on PC monitors, Mac lid close).
- Bitrate adaptation from receiver reports.
- Stats overlay on viewers (toggle via settings and hotkey).
- Structured logging: macOS unified logging; Windows rotating log files in `%LOCALAPPDATA%\Osplay\logs`; core logs routed into both. A "Copy diagnostics" action that bundles logs, versions and anonymized config for bug reports.
- Manual test matrix in `docs/TESTING.md`.
**Depends on**: Phase 11, Phase 12
**Acceptance criteria**:
- [ ] Each scenario in the test matrix passes on the reference hardware (M3 Air + the developer's PC).
- [ ] Waking either device restores the session automatically when both are awake and on the same network.
- [ ] A one-hour session shows no memory growth and no frame pacing degradation.

### Phase 14: Packaging, release and documentation
**Goal**: Anyone can install Osplay from GitHub Releases, and contributors can build it from the docs alone.
**Delivers**:
- macOS: Developer ID signing, Hardened Runtime, notarization and stapling in CI using repository secrets; DMG; Sparkle appcast published with releases.
- Windows: Inno Setup installer that installs the shell, engine, helper service, VDD (per the Phase 0 decision) and firewall rules; clean uninstall that removes the service, disables and optionally removes VDD, and deletes firewall rules.
- Release workflow triggered by a version tag, producing both artifacts and release notes.
- Final `README.md`: what it is and is not, how it works (diagram), screenshots/GIF, requirements, installation per platform, first-run walkthrough, security model summary, performance expectations, troubleshooting links, building from source, contributing, license, credits (VDD, Monocypher, Asio, Sparkle, WPF-UI, H.NotifyIcon).
- Final `docs/` set brought up to date.
**Depends on**: Phase 13
**Acceptance criteria**:
- [ ] The DMG passes Gatekeeper on a clean Mac without warnings.
- [ ] The Windows installer works on a clean Windows 11 machine without manual steps beyond SmartScreen (until signing is in place).
- [ ] Uninstall leaves no service, driver config, firewall rule or virtual monitor behind.
- [ ] A new contributor can build both apps using only `BUILDING.md`.

---

## Implementation Guidelines

These apply to ALL phases. Claude Code must follow them throughout development.

- Follow SOLID principles, especially Single Responsibility and Dependency Inversion. Every platform capability (virtual display, capture, encode, decode, present, discovery, key storage, permissions, hotkeys, power) sits behind an interface and is injected.
- Apply design patterns where they remove complexity: Strategy for encoder/decoder backends, Observer/async sequences for session events, State for the session lifecycle, Adapter for VDD and private APIs. Do not add patterns for their own sake.
- Clean, self-documenting code. Comments explain why, never what.
- No hardcoded values: protocol constants live in `core`; tunables (bitrate tables, timeouts, deadlines, retry limits) live in one constants file per platform; nothing user-tunable is buried in code.
- **Theming is centralized.** Every color, font, typography scale, spacing value, radius and shadow comes from the platform theme source. Views and XAML consume tokens only.
- **Localization is active.** Every user-facing string goes through `Localizable.xcstrings` or `.resx`, in English and Turkish. `core` and the native engine return error codes, never text.
- Error handling is explicit. C++: a `Result`-style return type or HRESULT mapping, no exceptions crossing the C API, no silent catches. Swift: typed error enums. C#: specific exceptions mapped from engine error codes. Every error reaching the user has an actionable localized message.
- Memory and resources: RAII everywhere in C++, no raw `new`/`delete`, `ComPtr` for COM. No force unwraps in Swift outside tests. The C API uses opaque handles with explicit create/destroy and documents thread affinity for every callback.
- Concurrency: `core` owns one I/O thread; callbacks never block it. Swift sessions are actors; UI updates on the main actor. Windows presentation runs on its own thread; the C# shell never calls into native code on the UI thread.
- Performance discipline: no CPU copies of frames in the hot path on either platform (IOSurface / D3D11 textures end to end); no allocations per packet in steady state in `core`.
- Security discipline: validate every length and index from the network before use; fuzz decoders; never log key material or full packet payloads.
- Testing: unit tests for every `core` module, shared fixtures for interop, integration tests for transport, a manual test matrix for hardware paths. CI must stay green after every phase.
- Commits: small, focused, conventional commit messages. Each phase ends with updated docs for anything it changed.
- Repository hygiene: README, `.gitignore`, LICENSE, CONTRIBUTING, SECURITY and third-party notices kept current.
- Developer-facing instructions say "terminal" for macOS/Linux steps and "command line" for Windows (PowerShell) steps.
- Respect existing code patterns: read and understand current code before changing it.

---

## Risks & Watch Items

- **`CGVirtualDisplay` breakage:** a macOS update can change or remove the private API. Keep it isolated behind one adapter, test on every macOS beta, and track how DeskPad/BetterDisplay respond.
- **M3 Air virtual display count:** validated in Phase 1; everything else depends on it.
- **VDD control limits:** if monitor count or resolution cannot change without a device restart, connection setup gets slower and may flicker other monitors. Measure it in Phase 2. If unacceptable, evaluate forking VDD or writing a minimal IddCx driver (which then requires driver signing through Microsoft's attestation process with an EV certificate).
- **Windows display re-enumeration:** VDD's own documentation warns Windows may prioritize the virtual display over a physical one during driver updates. Ensure the virtual monitor is never set as primary and document recovery.
- **Text sharpness:** 4:2:0 chroma blurs small colored text. Acceptable for MVP; HEVC 4:4:4 is the post-MVP path, gated on verified hardware support on both ends.
- **Wi-Fi variability:** packet loss bursts can cause IDR storms. NACK, IDR rate limiting and bitrate adaptation mitigate; FEC is post-MVP.
- **macOS TCC changes:** Screen Recording and Local Network permission behavior keeps changing across releases. Onboarding and runtime revocation handling must stay resilient.
- **Desktop Duplication edge cases:** hybrid GPU systems, HDR-enabled desktops and exclusive full-screen apps on the virtual monitor can break duplication. Document known limitations.
- **DRM content** (streaming services) appears black on both platforms. This cannot be solved; document it.
- **Windows code signing:** without a certificate, SmartScreen will warn. Apply to SignPath Foundation early.
- **Sparkle and notarization secrets in CI:** store signing identity, notarization credentials and the Sparkle EdDSA private key as repository secrets; never commit them. Document rotation.
