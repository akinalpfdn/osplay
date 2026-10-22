# Decisions

This file tracks all non-trivial technical decisions made during this project.
Entries are append-only. The stack itself is specified in `DEVPLAN.md`; only deviations and
choices the plan leaves open are logged here.

---

## 2026-10-17 — Repository visibility and branch flow
**Chosen:** User decision: public GitHub repository `akinalpfdn/osplay`. Every DEVPLAN phase is developed on its own branch (`feature/phase-XX-<slug>`) and lands through a pull request that the user merges once CI is green.
**Alternatives:** Private repository until MVP; committing directly to `main`.
**Why:** The project is open source by design, public repositories get unlimited Actions minutes, and a PR per phase gives a reviewable, revertable unit that matches the phase acceptance gates.
**Trade-offs:** Work is visible before it is usable; every phase needs a PR round-trip.
**Revisit if:** Outside contributors arrive and need a different integration model.

---

## 2026-10-17 — Identifier prefix
**Chosen:** User decision: `com.akinalpfdn.osplay` as the macOS bundle ID and the prefix for every reverse-DNS identifier (Keychain service, logging subsystem, Sparkle, helper names).
**Alternatives:** `com.akinalpfidan.osplay`, `io.github.akinalpfdn.osplay`.
**Why:** Matches the GitHub handle that owns the repository.
**Trade-offs:** Changing it later resets TCC permissions, Keychain items and saved display arrangement for existing users.
**Revisit if:** Never after the first public release.

---

## 2026-10-17 — Work tracking
**Chosen:** User decision: progress is tracked only in `.claude/phases/` and this file, not in Lore.
**Alternatives:** Lore work items and sessions.
**Why:** One source of truth that lives in the repository.
**Trade-offs:** No cross-project board view.
**Revisit if:** The user asks for board tracking.

---

## 2026-10-17 — Virtual Display Driver distribution
**Chosen:** Bundle the official signed VDD release binaries, pinned to an exact release, inside the Windows installer, with VDD's MIT license reproduced in `docs/THIRD_PARTY.md` and the installer's license page.
**Alternatives:** Download the release at install time; install through `winget`.
**Why:** VirtualDrivers/Virtual-Display-Driver is MIT licensed (verified 2026-10-17 via the GitHub license API: "Copyright (c) 2024 Virtual Display"), which permits redistribution of binaries as long as the copyright and permission notice are included. Bundling makes installs offline-capable, reproducible and immune to upstream release renames.
**Trade-offs:** Osplay must ship an installer update to pick up VDD fixes; installer size grows by the driver package.
**Revisit if:** VDD changes its license, or its signed packages stop being redistributable (e.g. signing terms of SignPath Foundation forbid it).

---

## 2026-10-17 — Pinned lint tool versions via PyPI wheels
**Chosen:** `clang-format` and `clang-tidy` are run through `uvx` at exact versions pinned in `scripts/lint.sh`; `swift-format` comes from the Xcode toolchain.
**Alternatives:** Homebrew `llvm` (unpinned, moves with every `brew upgrade`); distro packages on CI.
**Why:** Formatter output differs between major versions. Pinned wheels give byte-identical results on every developer machine and CI runner, on macOS, Windows and Linux.
**Trade-offs:** Requires `uv` on developer machines; first run downloads the wheels.
**Revisit if:** swift-format needs a pin too (Xcode updates change its rules).

---

## 2026-10-17 — macOS code signing configuration
**Chosen:** The committed project signs ad hoc (`CODE_SIGN_IDENTITY = -`). Developers put their team in a gitignored `macos/Config/Signing.local.xcconfig`, picked up by an optional `#include?`.
**Alternatives:** Committing `DEVELOPMENT_TEAM = WQ54PPL5VQ` in `project.yml`.
**Why:** A fresh clone must build for any contributor and on CI without an Apple account. The maintainer still gets a stable signature locally, which matters because TCC grants (Screen Recording) are bound to the code signature.
**Trade-offs:** The maintainer must create the local file once per machine (documented in `BUILDING.md`).
**Revisit if:** Release signing moves to CI (Phase 14) — that uses its own explicit settings.

---

## 2026-10-17 — Windows native build generator
**Chosen:** CMake presets use the Ninja generator with MSVC from a Visual Studio developer shell, entered through `vswhere` + `Enter-VsDevShell`.
**Alternatives:** `Visual Studio 17 2022` / `Visual Studio 18 2026` generators.
**Why:** Visual Studio generators are tied to one VS major version; GitHub's Windows images currently ship both VS 2022 and VS 2026 variants. Ninja + the dev shell works with whichever VS is installed and produces `compile_commands.json` for tooling.
**Trade-offs:** Builds must start from a developer shell (documented, and scripted in CI).
**Revisit if:** A dependency requires a VS-generator-only feature.

---

## 2026-10-17 — Branch and tag protection
**Chosen:** User decision: only `akinalpfdn` may change `main`. Ruleset "main protection" blocks deletion, force push and any update (push or merge) for everyone, and requires a PR with green `macOS` and `Windows` checks. The repository admin role (only `akinalpfdn`) bypasses all of it, force push included. Ruleset "release tag protection" applies the same owner-only rule to creating, moving and deleting `v*` tags.
**Alternatives:** Legacy branch protection; no bypass (owner bound by the rules too).
**Why:** Outside contributors work through forks and PRs; merging stays with the owner. Rulesets are the current GitHub mechanism and cover tags too.
**Trade-offs:** The bypass also applies to tools acting with the owner's credentials (gh CLI, Claude Code), so PR-per-phase stays a working rule rather than something GitHub enforces against the owner. Published tags can still be moved by the owner; the rule against doing that is a discipline, not a technical block.
**Revisit if:** Another person gets write access, or releases need technically immutable tags.

---

## 2026-10-17 — macOS virtual display default resolution
**Chosen:** Virtual displays are created with "looks like" modes at 1, ¾ and ½ of the remote monitor's pixel size and, when HiDPI is on, a 2× backing limit. Whoever creates a display picks the "crisp" HiDPI resolution (looks like half size, backing exactly the remote pixel size) as the default instead of macOS's own choice.
**Alternatives:** Let macOS choose; limit `maxPixels` to the native size so no larger backing exists.
**Why:** Measured in Phase 1: on a new identity macOS selects the largest 2× mode, i.e. a 5120×2880 backing for a 2560×1440 monitor — four times the pixels the stream carries, rendered and then scaled down during capture, for each of two displays on a MacBook Air. Capping `maxPixels` would remove the ¾ "more space" HiDPI option users expect.
**Trade-offs:** The default looks like a small 1280×720 desktop on a 1440p monitor; users who want more space pick a larger size in System Settings, which macOS then remembers per identity.
**Revisit if:** Phase 11 per-peer HiDPI settings define a different default, or capture cost at 2× backing turns out negligible.

---

## 2026-10-17 — Hardware tests outside CI
**Chosen:** Tests that create real virtual displays live in `OsplayHardwareTests`, run through a separate `OsplayHardware` scheme, manually on a Mac and after macOS updates.
**Alternatives:** Run them in the default scheme and CI; no automated hardware tests.
**Why:** They change the developer's screen layout while running, and hosted CI runners are not a reliable stand-in for the M3 Air the go/no-go decision is about. Logic is covered by unit tests with a fake provider in CI.
**Trade-offs:** A private-API break is only caught when someone runs the scheme.
**Revisit if:** CI gets a dedicated Mac with a known display setup.

---

## 2026-10-17 — Windows virtual monitor control
**Chosen:** OsplayHelper owns the Virtual Display Driver: it rewrites `vdd_settings.xml` (one monitor, one exact mode) and enables or disables the driver's device through SetupAPI. The monitor is tied to the helper's single pipe client: when that client disconnects, the console user changes or the service starts or stops, the device is disabled. The driver's own named pipe is not used.
**Alternatives:** Driving VDD through its pipe (`SETDISPLAYCOUNT`, `RELOAD_DRIVER`); keeping the device enabled with a hidden monitor; forking VDD.
**Why:** The driver reads its settings only when the device starts and coerces a count of 0 to 1 (source reading, Phase 2), so neither modes nor removal are possible through the pipe; its pipe also accepts any local user. Tying the monitor to the client connection guarantees that a crashed engine never leaves an invisible monitor that windows move onto.
**Trade-offs:** Every session start and resolution change restarts the device (a short display reconfiguration). Osplay conflicts with other software that uses the same VDD installation.
**Revisit if:** Device restarts prove too slow or disruptive on real hardware (then: fork VDD or a minimal IddCx driver, per DEVPLAN risks).

---

## 2026-10-17 — Portable Windows libraries
**Chosen:** The helper protocol, request handler, settings renderer and engine provider logic live in Windows-header-free libraries (`windows/shared`, `osplay_engine_logic`) that build, test and get clang-tidy on macOS and Linux through the `portable` preset, as well as on Windows.
**Alternatives:** Keep all Windows code Windows-only and rely on the Windows CI job.
**Why:** The maintainer develops on a Mac; logic that can be tested locally gets tested before CI and catches errors earlier than a remote MSVC build.
**Trade-offs:** Windows-specific layers (SetupAPI, pipes, service) stay thin and are verified only on Windows.
**Revisit if:** Never needed; the split costs little.

---

## 2026-10-17 — Protocol fixtures from an independent encoder
**Chosen:** Binary fixtures are written by a Python encoder (`protocol/tools/generate_fixtures.py`) that follows `docs/PROTOCOL.md` and shares no code with the C++ codec. Tests check that every valid fixture decodes to the documented field values and re-encodes byte for byte, and that every malformed fixture fails with its documented error. CI fails if the files differ from the generator's output.
**Alternatives:** Generating fixtures with the C++ encoder; hand-written hex files.
**Why:** Fixtures produced by the implementation under test only prove it agrees with itself. Two independent encoders that agree byte for byte catch misreadings of the specification. A generator keeps 82 files reviewable as code.
**Trade-offs:** A protocol change has to be made in the document, the generator and the codec.
**Revisit if:** A third implementation (e.g. a C# decoder) appears; it would test against the same fixtures.

---

## 2026-10-17 — CoreBridge lives in the Swift package
**Chosen:** `CoreBridge`, the Swift wrapper over the C API, is a target of the root Swift package (`macos/CoreBridge/`), not part of the app target. The app depends on the `CoreBridge` product; `swift test` runs the fixture tests through it.
**Alternatives:** Keep CoreBridge in the app target (DEVPLAN's layout) and test it through the app's test bundle.
**Why:** DEVPLAN asks for Swift tests via CoreBridge against the shared fixtures; as a package target they run with `swift test` in seconds, without building and hosting the app, and the wrapper's public API is explicit.
**Trade-offs:** One more module boundary; CoreBridge types must be `public`.
**Revisit if:** CoreBridge needs app-only frameworks.

---

## 2026-10-17 — MediaSetup control message
**Chosen:** A 20th control message, MediaSetup (host → viewer: UDP port and session token), in addition to DEVPLAN's list.
**Alternatives:** Put the token in Capabilities; advertise the UDP port through Bonjour.
**Why:** DEVPLAN requires a session token issued over the control channel for the UDP association, and the viewer also needs the host's ephemeral UDP port. Capabilities are symmetric and sent by both sides; a dedicated host message keeps both concerns explicit.
**Trade-offs:** None significant.
**Revisit if:** Media moves to a different transport.

---

## 2026-10-17 — Platform-supplied randomness for core
**Chosen:** Core obtains random bytes through an injected `crypto::RandomSource`; the C API takes an `osplay_random_source` callback that platforms back with `SecRandomCopyBytes` / `BCryptGenRandom`.
**Alternatives:** `std::random_device`; calling OS APIs from core.
**Why:** DEVPLAN keeps platform headers out of core. `std::random_device` is not guaranteed to be cryptographically secure (some toolchains have shipped deterministic implementations). Injection also makes the handshake reproducible in tests, which is how the published Noise vectors are checked.
**Trade-offs:** Every API that needs randomness takes a source; platforms must wire it.
**Revisit if:** Core gains a portable, vetted CSPRNG dependency.

---

## 2026-10-17 — Session key derivation and pairing code
**Chosen:** Control channel keys are Noise's standard `Split()`; media keys come from a second HKDF over the final chaining key with the label "Osplay media keys v1". The pairing code is BLAKE2b-256("Osplay pairing code v1" || handshake hash), first 8 bytes little-endian, mod 10^6. The viewer is the Noise initiator.
**Alternatives:** Using the Split keys for both channels with separate nonce spaces; deriving media keys from the Split keys; a pairing code from the first bytes of the hash without a label.
**Why:** DEVPLAN requires separate keys per direction and per channel. Keeping the control channel exactly Noise transport lets the published vectors cover it. Labels give domain separation, so no derived value can be confused with another use of the same secret.
**Trade-offs:** The media derivation is an Osplay extension that other Noise libraries would need to replicate; it is documented in PROTOCOL.md.
**Revisit if:** A Noise revision standardises extra key derivation.

---

## 2026-10-17 — Sans-I/O session engine under a thin Asio transport
**Chosen:** The whole session protocol (handshake, pairing, negotiation, heartbeat, loss recovery) is a `SessionEngine` that takes bytes, datagrams and time and produces bytes, datagrams and callbacks; `AsioSession` only moves bytes and runs a 2 ms timer on one I/O thread per session.
**Alternatives:** Protocol logic inside Asio completion handlers; one shared I/O thread for all sessions.
**Why:** Loss, reordering, outages and timeouts can be tested deterministically and fast with a simulated network and clock, which the acceptance criteria require (5% loss, 3 s outage). One thread per session matches DEVPLAN ("core owns one I/O thread") and there is one session per device.
**Trade-offs:** A command from the platform waits for the I/O thread (bounded by 5 s); real-socket behaviour is covered by a smaller set of loopback and relay tests.
**Revisit if:** A device needs several concurrent sessions.

---

## 2026-10-17 — One PairingConfirm per side, also for trusted peers
**Chosen:** After the handshake each side sends exactly one PairingConfirm: immediately when it trusts the peer's key, after the user decides when the key is unknown.
**Alternatives:** PairingConfirm only during a first pairing.
**Why:** Trust is decided per side. A Mac that still trusts a Windows PC whose user removed the pairing must not start streaming; an explicit answer from both sides makes the state machine the same for both cases.
**Trade-offs:** One extra small message per connection.
**Revisit if:** Never expected.

---

## 2026-10-17 — MAX_ENUM sentinels in the C API
**Chosen:** Every C enum in `osplay.h` ends with `*_MAX_ENUM = 0x7FFFFFFF`; values from bindings are range-checked.
**Alternatives:** Taking `uint32_t` parameters instead of enum types.
**Why:** Swift and C# can pass any 32-bit value. Without the sentinel, an out-of-range value is undefined behaviour in C++ (UBSan reported it in Phase 5). The sentinel keeps typed parameters and the existing bindings unchanged (the same convention as Vulkan).
**Trade-offs:** One unused value per enum that bindings will see.
**Revisit if:** The header moves to C23 enums with a fixed underlying type.

---

## 2026-10-17 — Keyframes for unchanged screens re-encode the last frame
**Chosen:** A keyframe request makes the host encode the last captured frame again immediately, as a keyframe with a fresh timestamp.
**Alternatives:** Wait for the next captured frame; capture periodic frames on idle screens.
**Why:** ScreenCaptureKit produces no frames while nothing changes, so a viewer that starts or recovers from loss would see nothing until the screen moves. Re-encoding the retained IOSurface costs one encode and no capture.
**Trade-offs:** One extra capture buffer stays retained per stream (queue depth raised to 5).
**Revisit if:** ScreenCaptureKit gains a way to request a frame on demand.

---

## 2026-10-17 — Low-latency rate control with a realtime fallback
**Chosen:** VideoToolbox sessions request `EnableLowLatencyRateControl`; if creation fails, a plain realtime session is used and logged. Only realtime, no reordering, profile and bitrate are mandatory properties; frame rate, keyframe interval, color tags, speed priority and peak data rate are applied when supported.
**Alternatives:** Fail when low-latency mode is unavailable; require hardware encoding.
**Why:** Apple Silicon supports low-latency mode, but virtual machines (CI) and older encoders may not; failing there would make the pipeline untestable and brittle across macOS releases.
**Trade-offs:** On the fallback path rate control allows larger bursts and slightly higher latency.
**Revisit if:** Telemetry shows the fallback on real user hardware.

---

## 2026-10-17 — Debug host test streams to an in-process viewer
**Chosen:** The Phase 6 acceptance harness runs a real host session (virtual displays, ScreenCaptureKit, VideoToolbox, core) over loopback into a headless core viewer in the same app, which measures pacing, bitrate and latency; a moving test pattern fills each virtual display.
**Alternatives:** Measure only at the encoder output; wait for the Phase 7 viewer.
**Why:** It exercises the whole host path including packetization and reassembly, without a second device or decoder, and the pacing figures are what the viewer will actually receive.
**Trade-offs:** Debug-only code that Phase 7's visual loopback partly supersedes.
**Revisit if:** Phase 7's loopback viewer covers the same measurements.

---

## 2026-10-18 — Capture interval with headroom
**Chosen:** `SCStreamConfiguration.minimumFrameInterval` is half the stream's frame interval (`Tunables.Capture.frameRateHeadroom = 2`).
**Alternatives:** Exactly one frame interval (1/60 s).
**Why:** Measured on the M3 Air with two 1440p60 virtual displays: with exactly 1/60 s ScreenCaptureKit dropped frames that arrived slightly early (56.3 fps, frequent 33 ms gaps); with headroom 59.1 fps and a p99 interval of 21.5 ms. The virtual display's refresh rate still caps the frame rate, so the headroom adds no frames.
**Trade-offs:** If a display ever refreshes faster than the stream, capture could exceed the stream's rate.
**Revisit if:** Virtual displays run at a higher refresh than their stream.

---

## 2026-10-18 — Default viewer hotkey ⌃⌥⌘V
**Chosen:** User decision: ⌃⌥⌘V toggles the viewer (hide/show); configurable in Phase 11.
**Alternatives:** ⌃⌥⌘Esc; ⌃⌥⌘V plus Esc inside the viewer window.
**Why:** Unlikely to collide with other apps, close to ⌥⌘Esc (Force Quit) without the risk of hitting it.
**Trade-offs:** Not discoverable without the onboarding/settings UI.
**Revisit if:** Users report conflicts.

---

## 2026-10-18 — Lost cursor shapes come back through a keyframe request
**Chosen:** Shapes are never retransmitted; a viewer that sees visible positions naming a shape it lacks for 100 ms sends KeyframeRequest, and the host resends the shape with every keyframe.
**Alternatives:** Periodic shape resends; NACKs for shape fragments; a new control message.
**Why:** Keeps the protocol unchanged and costs nothing in steady state; shapes change rarely and loss is rare on a LAN. A keyframe is a few hundred kilobytes at worst.
**Trade-offs:** Recovering a lost shape costs a keyframe; a cursor that does not move is not recovered until it does.
**Revisit if:** Wi-Fi tests show frequent keyframes caused by cursor loss.

---

## 2026-10-18 — Full-screen viewer as a borderless window above the menu bar
**Chosen:** A borderless window at menu bar level + 1 covering the target display, on all Spaces, instead of a native full-screen Space.
**Alternatives:** `toggleFullScreen` (own Space); `NSApplication.presentationOptions` hiding menu bar and Dock.
**Why:** A Space switch animates and takes time, and moves away from the user's other Spaces; presentation options only apply while the app is frontmost, which a menu bar app often is not. The window appears and disappears instantly, which the hotkey relies on.
**Trade-offs:** The menu bar and Dock of that display are covered, not hidden; system alerts can appear beneath.
**Revisit if:** macOS restricts windows above the menu bar.

---

## 2026-10-18 — Windows host pipeline on one thread, orchestration portable
**Chosen:** One pipeline thread owns the D3D11 device, Desktop Duplication, the video processor and the async Media Foundation encoder (events polled without blocking between frames). The session logic (`HostOrchestrator`), cursor conversion and keyframe repair are portable C++ in `osplay_engine_logic`, tested on macOS and Linux CI.
**Alternatives:** Separate capture and encoder threads with a blocking MF event loop; orchestration inside the Windows-only C API code.
**Why:** A single owner avoids cross-thread COM and texture hand-off problems and makes shutdown simple; polling adds at most 1 ms. The Windows code can only be compiled on CI, so everything that is not an API call lives where it can be unit tested.
**Trade-offs:** A slow encoder output delays the next capture poll by up to 1 ms; the Win32 parts are verified only by the developer's manual run.
**Revisit if:** Profiling shows the loop limits frame rate at higher refresh rates.

---

## 2026-10-18 — Screen-inverting cursor pixels as black with a white outline
**Chosen:** Monochrome AND 1 / XOR 1 pixels and masked-colour XOR pixels, which invert the screen, are drawn opaque black with a one-pixel white outline.
**Alternatives:** Opaque black only; opaque white; a separate inversion flag in the protocol.
**Why:** BGRA cannot express inversion. The text beam is made of inverting pixels; black with an outline stays visible on light and dark content without a protocol change.
**Trade-offs:** The beam looks slightly heavier than on Windows.
**Revisit if:** The protocol gains a blend mode for cursors.

---


## 2026-10-18 — User decision: Windows viewer hotkey Ctrl+Alt+Shift+V
**Chosen:** Ctrl+Alt+Shift+V hides and shows the Windows viewer.
**Alternatives:** Ctrl+Alt+V.
**Why:** On Turkish and other AltGr layouts Ctrl+Alt is AltGr, so Ctrl+Alt+V would collide with typing.
**Trade-offs:** A four-key chord; it becomes configurable with the settings UI.
**Revisit if:** Users report a conflict.

---

## 2026-10-18 — User decision: Phase 9 two-stream test through a temporary IP connection
**Chosen:** A debug "Host for a remote viewer" on the Mac and `OsplayViewerTest --connect HOST PORT` on Windows; both trust any peer and are removed when Phase 10 adds discovery.
**Alternatives:** Wait for Phase 10 to test two streams; a Windows-only test with two virtual monitors.
**Why:** The Windows host streams one monitor only, so two real streams need the Mac host; discovery does not exist yet.
**Trade-offs:** Throwaway code in both apps; the debug host trusts any viewer while it runs.
**Revisit if:** Phase 10 starts.

---

## 2026-10-18 — Windows viewer presents from one decode thread per stream
**Chosen:** Each stream's thread decodes (MF H.264 decoder on D3D11), converts with the video processor and presents a `FLIP_DISCARD` swap chain (waitable, maximum latency 1) at once on a borderless window without a redirection bitmap; one UI thread owns only windows, the hotkey and display changes.
**Alternatives:** A DirectComposition or composition swap chain; a pixel shader for NV12 → RGB; presenting all streams from the UI thread.
**Why:** A plain flip-model window that covers its monitor is what DWM promotes to independent flip, which removes a composition frame of latency. Separate threads keep one slow monitor from delaying the other. The video processor is the hardware path the encoder side already uses.
**Trade-offs:** Overlays drawn above the window (notifications) drop it back to composition while visible; no custom scaling filter.
**Revisit if:** PresentMon shows composed flip on common setups, or the video processor is slow on some GPUs.

---

## 2026-10-18 — Viewer monitor ids by desktop position, matched by GDI name
**Chosen:** Offered monitors are numbered left to right (then top to bottom); after a display change they are matched by GDI device name to decide which are still attached.
**Alternatives:** Ids from the CCD target id; matching by EDID name.
**Why:** Position ids are small, stable while nothing changes and match what the user sees; GDI names survive the renumbering an unplug causes. EDID names are not unique when two identical monitors are attached.
**Trade-offs:** Windows may reuse a GDI name for a different monitor plugged in later during the same session.
**Revisit if:** Monitor ids need to persist across sessions (per-monitor settings).

---

## 2026-10-19 — Streams capped at 60 Hz until high refresh support
**Chosen:** Both hosts stream at most 60 Hz; a viewer monitor at 120 or 144 Hz gets a 60 Hz virtual display and stream.
**Alternatives:** Stream at the monitor's rate; cap only the Mac host.
**Why:** DEVPLAN lists 120 Hz streams as post-MVP. Measured on the MacBook Air M3: a 1440p120 virtual display captures at only ~80 fps next to a 1080p60 one, and the Phase 9 PC test showed 40–65 ms latency on that stream against 3–13 ms at 60 Hz.
**Trade-offs:** High refresh monitors show 60 fps motion.
**Revisit if:** The post-MVP high refresh work starts.

---

## 2026-10-19 — Clock offset from the fastest recent ping
**Chosen:** The peer clock offset is the offset of the ping exchange with the smallest round trip among the last eight (one per second), as NTP does.
**Alternatives:** An exponential average of every sample (the previous behaviour); discarding samples above a fixed round trip.
**Why:** In the Phase 9 test over Wi-Fi a stalled exchange (round trip of seconds) shifted the average by up to half its round trip, so reported latency jumped to 1–3 s and took about half a minute to settle. The fastest exchange is the one closest to symmetric; a fixed threshold would need tuning per network.
**Trade-offs:** A genuine clock step is picked up only once a fast exchange carries it (within 8 s).
**Revisit if:** Latency statistics drive behaviour (adaptive bitrate) rather than display only.

---

## 2026-10-19 — Initial virtual display resolution follows the viewer's scaling
**Chosen:** A viewer monitor the user runs below 150% scaling starts at one point per pixel (2560×1440 looks like 2560×1440); at 150% and above it starts in the crisp HiDPI resolution (looks like half size). Refines the 2026-10-18 crisp-HiDPI default.
**Alternatives:** Crisp HiDPI for every monitor (the previous default); macOS's own default (largest 2× mode, four times the pixels).
**Why:** In the Phase 9 test both Windows monitors run at 100%, and the crisp default made them look like a 1280×720 desktop — not what the user sees on Windows. The viewer already reports its scaling, so the host can match it; both choices keep the backing equal to the stream size.
**Trade-offs:** Text at one point per pixel is not HiDPI-smoothed on a low-density monitor, as on Windows itself.
**Revisit if:** Phase 11 per-peer display settings let the user choose.

---

## 2026-10-19 — User decision: the WPF tray menu is Phase 10's Windows UI
**Chosen:** Discovered hosts, role, connect and the pairing dialog in the existing WPF tray, with the real P/Invoke layer (`LibraryImport`, function pointers).
**Alternatives:** A command-line tool for discovery and pairing.
**Why:** It is the closest to real use and the interop layer is reused by Phase 12's UI.
**Trade-offs:** More work now; the tray menu is temporary.
**Revisit if:** Phase 12 starts.

---

## 2026-10-19 — macOS Bonjour through the DNS-SD C API, not Network.framework
**Chosen:** `DNSServiceRegister` / `DNSServiceBrowse` / `DNSServiceResolve` on a serial queue.
**Alternatives:** `NWListener` advertising and `NWBrowser` (DEVPLAN named Network.framework).
**Why:** The core owns the listening socket, and `NWListener` can only advertise a port it owns itself. `NWBrowser` returns service endpoints without a port; resolving one means opening a connection, which would take the host's single viewer slot.
**Trade-offs:** C callbacks with unmanaged context pointers in Swift.
**Revisit if:** Network.framework gains resolve-without-connect, or the core hands its socket to the platform.

---

## 2026-10-19 — Identity and trust handled inside the Windows engine
**Chosen:** Sessions started without a secret key load the DPAPI identity, answer trust queries from the stored peers and store pairings themselves; the shell only shows the code and lists or revokes peers.
**Alternatives:** The shell loads the secret and answers every trust query over P/Invoke.
**Why:** The secret key never enters managed memory, and the core's trust query (which blocks its I/O thread) never waits for the UI thread.
**Trade-offs:** Developer tools keep passing their own keys to get the old behaviour.
**Revisit if:** Several users share one Windows account and need separate identities.

---

## 2026-10-19 — Revoked devices are kept and refused
**Chosen:** Revoke marks the peer; its key is answered with "blocked" and the session fails with `NOT_TRUSTED`. Removing the peer lets it pair again. Revoking or removing a device also ends a session already running with it, on both platforms.
**Alternatives:** Revoke deletes the peer (the next connection would show a pairing code).
**Why:** DEVPLAN requires a revoked device to be refused, not invited to pair again.
**Trade-offs:** Re-pairing a revoked device takes two steps.
**Revisit if:** Users find the distinction confusing (Phase 11/12 UI).

---

## 2026-10-19 — User decisions: native look, recordable hotkey, corner stats overlay
**Chosen:** System fonts, colours and SF Symbols with a Control Center–like menu bar window; the hide/show shortcut is recorded in Settings; the stats overlay is a small box in the top-left corner of the watched display.
**Alternatives:** A branded palette; a fixed shortcut; stats only in the menu bar window.
**Why:** The user chose these; a native look follows light/dark and accessibility settings without extra work.
**Trade-offs:** Less visual identity; a recorder needs conflict handling (another app owning the combination is reported).
**Revisit if:** The project gets a visual identity.

---

## 2026-10-19 — Quality presets as bits per pixel per frame
**Chosen:** Balanced 0.06, High 0.10 (the previous fixed value, default), Ultra 0.16 bits per pixel per frame, clamped to 2–80 Mbit/s; the host reads the preset when a viewer's monitors arrive.
**Alternatives:** Fixed bitrates per preset.
**Why:** DEVPLAN asks for presets that scale with resolution and refresh; High keeps the measured Phase 6–9 behaviour.
**Trade-offs:** A preset change applies from the next connection, not mid-stream.
**Revisit if:** Adaptive bitrate arrives.

---

## 2026-10-19 — Local Network permission read from a Bonjour browse
**Chosen:** The permissions window shows Local Network as allowed when a Network.framework browse for `_osplay._tcp` becomes ready and as denied when it waits with a policy error.
**Alternatives:** Not showing a status (macOS has no query API).
**Why:** The Phase 10 test showed a denied permission silently hides every device; a visible status is the only way users find the setting.
**Trade-offs:** Until macOS answers, the status is unknown.
**Revisit if:** macOS adds an API for the Local Network permission.

---

## 2026-10-19 — Osplay arranges the virtual displays itself
**Chosen:** Settings › Displays has an arrangement canvas per paired device, like the old System Settings Arrange view: this Mac's main display and each of the device's monitors as boxes. A dropped monitor snaps to an edge without overlapping (`DisplayLayout`), the arrangement is saved per device, applied at once to a running session and to every later connection (`CGConfigureDisplayOrigin`, saved permanently). Without a saved arrangement the monitors start in a row to the right.
**Alternatives:** Relying on System Settings; a fixed position per device (left/right/above/below).
**Why:** On macOS 26 System Settings shows neither an Arrange button nor draggable display icons with these displays attached, so the user could not arrange them at all; a hardware test shows the window server accepts the move from code. The user chose free dragging over a position list.
**Trade-offs:** The canvas needs one connection to learn the monitors' sizes; the arrangement only covers this Mac's main display and that device's monitors.
**Revisit if:** System Settings offers arrangement for these displays again.

---

## 2026-10-19 — User decision: "Extend this Mac" names the hosting role
**Chosen:** The Mac's hosting role is "Extend this Mac" ("Bu Mac'i genişlet"); the viewer role stays "Use as display". Both are cards with a one-line explanation instead of a segmented control.
**Alternatives:** "Share screens"; "Use other screens"; "Add displays".
**Why:** The user found "Share screens" unclear; a segmented control truncated the longer Turkish labels.
**Trade-offs:** The menu window is taller.
**Revisit if:** Usability feedback.

---

## 2026-10-20 — The host repeats a stream's last fragment while the screen is still
**Chosen:** After a stream's newest frame, its final fragment is sent again at 50, 200 and 700 ms unless a newer frame went out. A viewer that lost the whole frame learns of it from the repeat and NACKs the rest.
**Alternatives:** A new control message announcing the newest frame ID; periodic keyframes; repeating the whole last frame.
**Why:** A viewer finds a lost frame only when a later fragment arrives. When the last frame before the screen goes still is lost entirely (a full send buffer drops datagrams), nothing ever reveals it and the viewer keeps the previous picture; the core's loopback test hit this under ThreadSanitizer. Repeating one fragment fixes it without changing the protocol and costs three small datagrams per pause.
**Trade-offs:** Recovery after such a loss takes up to 50 ms plus a round trip; if all three repeats are lost too, the picture stays old until the screen changes.
**Revisit if:** The protocol gets a newest-frame announcement in another message (e.g. a host report).

---

## 2026-10-20 — User decision: the Windows shell mirrors the Mac's menu bar app
**Chosen:** A tray icon whose click opens a panel (roles, devices, monitors, quality) and a separate Settings window, instead of a main window with a navigation pane.
**Alternatives:** A Fluent main window with a left navigation pane (Home, Devices, Settings) opened from the tray.
**Why:** The user wanted both apps to work the same way; Osplay is used in short visits, which a panel suits.
**Trade-offs:** The panel has a fixed width, so long lists scroll less comfortably than in a window.
**Revisit if:** The panel grows beyond what fits above the taskbar.

---

## 2026-10-20 — The engine draws the Windows stats overlay; the shell writes its text
**Chosen:** `osplay_engine_viewer_set_overlay(stream, text)` takes one UTF-8 line that the shell formats (localized) from `on_stats`; the stream thread draws it with Direct2D/DirectWrite on the swap chain. While it shows, frames go through a kept texture so the text can be redrawn without a new frame.
**Alternatives:** A WPF topmost window over the viewer; the engine formatting the text itself.
**Why:** A window over the fullscreen swap chain would end independent flip and add latency; the engine returns codes, never user-facing text (DEVPLAN), so the shell owns the wording.
**Trade-offs:** One extra GPU copy per frame while the overlay is on.
**Revisit if:** The overlay needs more than one line or per-monitor placement.

---

## 2026-10-20 — Every engine call from the shell runs on one serial queue
**Chosen:** `Native.Run` chains every P/Invoke call as a thread pool task, one at a time; view models await it.
**Alternatives:** `Task.Run` per call with a lock per handle; a dedicated engine thread.
**Why:** DEVPLAN forbids native calls on the UI thread, engine.h forbids overlapping calls on a handle, and a destroy must never race a call on the same handle; one queue guarantees all three with no per-handle bookkeeping.
**Trade-offs:** A slow call (a session teardown) delays unrelated calls such as listing monitors.
**Revisit if:** A call routinely takes long enough to make the UI feel stuck.

## 2026-10-20 — User decision: an instant role swap comes right after Phase 12
**Chosen:** A new DEVPLAN Phase 12b (phase file PHASE-016-SWAP-01): one shortcut on either computer reverses host and viewer of a running session, through a new control message; it runs before Phase 13.
**Alternatives:** After Phase 13; inside Phase 13 with the reconnect logic.
**Why:** The user sees it as the feature that sets Osplay apart and wants it next.
**Trade-offs:** A protocol change before the robustness phase; a swap takes seconds (virtual display creation and the first keyframe), and windows on a removed virtual display move to the main screen.
**Revisit if:** Swap time is too long to feel useful.

## 2026-10-20 — Role swap is one control message with request, accept and refuse
**Chosen:** RoleSwap (0x14) with a step field; the accepting side sends accept then Bye(5) and both sessions close; the former viewer hosts and the former host reconnects by device ID through Bonjour. Simultaneous requests are settled by the host accepting. An unanswered request expires after 10 s locally. Protocol minor version 1; requests are not sent to minor-0 peers.
**Alternatives:** Only Bye(5) without asking (the other side might be unable to host and both would end up disconnected); turning the running connection around in place (a host and a viewer are different sessions with different sockets, so it would need a new session anyway); keeping both directions open at once.
**Why:** Asking first lets the side that would host check its permission, driver and service before anything closes, so a refused swap leaves the session untouched; reconnecting through the normal discovery and session paths needs no new transport code.
**Trade-offs:** A swap takes seconds (new virtual displays, the first keyframe) and windows on a removed virtual display move to the main screen.
**Revisit if:** Measured swap times are too long; then keep a warm host listener on the viewer side.

## 2026-10-20 — Bitrate adaptation lives in the core, driven by frame loss and resends
**Chosen:** Each host stream has a BitrateController: 75% on an interval with ≥1% lost frames or ≥5% resent fragments, +8% after three clean intervals, between a quarter of the configured bitrate (at least 1 Mbit/s) and the configured bitrate; changes under 5% are not passed on. The platform encoders only apply `on_bitrate_target`.
**Alternatives:** Per-platform controllers in the apps; delay-based congestion control (send-time vs. receive-time gradients); renegotiating StreamConfig.
**Why:** One implementation for both hosts, unit-tested with the simulated network; the reports already carry loss, and resends show congestion before frames are lost. Delay-based control needs per-packet timing the protocol does not report yet.
**Trade-offs:** It reacts once per second and only to loss, so a link that is slow but lossless is not detected.
**Revisit if:** Wi-Fi tests show stalls without loss; then add RTT growth as a congestion signal.

---

## 2026-10-20 — Interruptions end the session and the viewer reconnects
**Chosen:** Sleep sends Bye 3 (new result PEER_SLEEPING on the other side); timeouts, network errors, a sleeping peer and a changed viewer monitor mode all end the session, and the viewer waits for the same host by device ID and reconnects. A user disconnect never reconnects.
**Alternatives:** Keeping sessions alive across sleep; renegotiating monitors inside a running session (a second ViewerMonitors).
**Why:** A new session reuses the paths that already work (discovery, handshake, trusted keys, display creation) and recovers from any socket or display state left behind by sleep; renegotiation would need protocol and orchestration changes on both hosts for a rare event.
**Trade-offs:** Recovery takes a few seconds (a new handshake and first keyframe); windows on removed virtual displays move to the main screen.
**Revisit if:** Users find reconnects after display changes too slow.

## 2026-10-20 — A host keeps its control port across sessions
**Chosen:** Each new host session listens on the previous session's TCP port (falling back to an ephemeral one when it is taken), so the Bonjour advertisement viewers already resolved stays right.
**Alternatives:** Resolving the service again before every connection attempt; a fixed well-known port.
**Why:** A host listens anew after every session; with a new ephemeral port each time, a viewer whose browse saw no change (Bonjour reports instances, not changed SRV records) kept connecting to the closed old port, so Disconnect followed by Connect never worked. Reusing the port needs no change in either app's discovery.
**Trade-offs:** If the port is taken in between, the stale-port case returns until the advertisement changes; a changed IP address is not covered.
**Revisit if:** Users hit failed reconnects after IP changes; then resolve again before each connection.

## 2026-10-21 — User decision: every direction, every monitor, several viewers — before release
**Chosen:** New DEVPLAN Phase 13b (SYM-01…03) before Phase 14: Windows hosts make one virtual display per viewer monitor, Mac viewers offer every display, and hosts serve several viewers at once; Mac→Mac and PC→PC are first-class.
**Alternatives:** Ship with the original asymmetry (Windows host: one display) and add this later; only symmetry now, several viewers later.
**Why:** The single-display Windows host came from an early assumption (a single-screen MacBook as the viewer) that was never put to the user; a laptop extending onto a two-monitor PC, or onto two other laptops, is a core use.
**Trade-offs:** Release moves later; the Windows multi-monitor work changes the helper protocol and needs testing on the developer's hardware.
**Revisit if:** —

## 2026-10-22 — The host paces large frames; bitrate adaptation reacts less
**Chosen:** Frames of more than 4 fragments are spread over half the frame interval (shared among streams) instead of one burst. Bitrate adaptation now drops to 85% (was 75%) and counts resends as congestion only from 20% (was 5%).
**Alternatives:** Only lowering the bitrate further on loss; FEC; a larger socket send buffer.
**Why:** On Wi-Fi the user saw constant loss and grainy video while scrolling, with good fps and ping: a keyframe or scroll frame of dozens of datagrams sent at once overflows the access point's queue. Each loss then lowered the bitrate steeply, which made the picture grainy even when resends had already repaired the frames.
**Trade-offs:** A large frame arrives up to half a frame interval later (about 8 ms at 60 Hz); a burst outage that begins mid-frame now costs that frame instead of only later ones.
**Revisit if:** Latency tests (Q4) get worse, or loss stays high on Wi-Fi; then consider FEC.

## 2026-10-22 — Up to three NACKs per frame, with a deadline that allows them
**Chosen:** A frame is NACKed at half a frame interval and again every max(half a frame interval, 1.5 RTT) while unrepaired, at most 3 times; it is dropped after half a frame interval plus 3 of those intervals without progress (about 33 ms on a quiet link, about 100 ms at 20 ms RTT).
**Alternatives:** One NACK and a deadline of one frame interval plus RTT (DEVPLAN's original rule); FEC.
**Why:** Over Wi-Fi the Windows viewer's log showed 65 NACKs, 17 dropped frames and 16 keyframe requests in 30 s: one NACK left about 9 ms of margin against 15–24 ms RTT with jitter, so a quarter of the repairs came too late, and each became a keyframe wait that froze the picture and lowered the bitrate.
**Trade-offs:** A frame that is truly lost now holds the stream up to about 100 ms before the keyframe request instead of about 40 ms; host resends grow on lossy links.
**Revisit if:** Latency tests (Q4) show long holds, or loss is mostly whole bursts where repeats do not help.
