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

## 2026-10-22 — User decision: one NACK per frame stays
**Chosen:** A frame is NACKed once, halfway through the frame interval, and dropped after one frame interval plus max(RTT, 2 ms) without progress, as DEVPLAN specifies.
**Alternatives:** Up to three NACKs per frame, spaced 1.5 RTT apart, with a deadline long enough for them (tried on this branch and reverted).
**Why:** On the user's Wi-Fi, dropped frames fell only from 0.57 to 0.41 per second, while every frame that was really lost held the picture about 100 ms instead of about 40 ms before the keyframe request; the user saw it as stutter. That repeats barely helped points to loss in bursts, where the repairs are lost too.
**Trade-offs:** A repair that arrives later than one frame interval plus RTT is wasted, and the frame costs a keyframe.
**Revisit if:** Loss turns out to be isolated packets rather than bursts (for example once the Mac's AWDL is ruled out), or if FEC is added.

## 2026-10-25 — Session sockets are marked as real-time traffic on macOS
**Chosen:** On macOS the media socket gets the network service type "interactive video" and the control socket "signaling" (`SO_NET_SERVICE_TYPE`). Other platforms are unchanged for now.
**Alternatives:** An opt-in setting that turns AWDL off during sessions through a privileged helper; documenting `sudo ifconfig awdl0 down` only; DSCP marking on Windows through qWAVE.
**Why:** The remaining Wi-Fi loss was AWDL: in the Mac → Mac test drops went to zero with `awdl0` down on both Macs. Apple documents the service type as what selects the Wi-Fi access category and tells the stack a latency-sensitive flow is running; it costs a few lines, needs no privileges and is correct for this traffic. User decision: mark first and measure, and only then consider the helper.
**Trade-offs:** Whether macOS shortens AWDL's off-channel time for marked traffic is not documented; it has to be measured with AWDL on.
**Revisit if:** The measurement shows little change (then the opt-in AWDL toggle), or Windows hosts show loss that DSCP priority would help.

## 2026-10-25 — User decision: streams are capped at 1080p by default, chosen on the extending computer
**Chosen:** A new phase (DEVPLAN 13c, STR-01) caps each stream at 720p, 1080p (default), 1440p or Native, fitted to the monitor's aspect ratio; the virtual display keeps the viewer monitor's resolution and the viewer scales the stream up. The setting sits next to quality on the extending computer. No bandwidth warning and no network test.
**Alternatives:** Making the virtual display itself 1080p (rejected: everything on it gets bigger); a setting on the viewing computer; a bandwidth estimate with a warning, or a throughput test between the two computers (rejected by the user).
**Why:** In the Mac → Mac test an M1 host extending onto a 3420×2214 MacBook and a 4K monitor managed 0–36 fps per stream and lost so much over Wi-Fi that both displays were unusable. A 1080p cap cuts pixels and bitrate to about a quarter; the encoder and the link both have room again.
**Trade-offs:** Text on a large viewer display is softer than at Native; Native stays available for strong hosts and fast links.
**Revisit if:** Typical hosts and links handle Native comfortably, or a 1440p default looks clearly better at the same cost.

## 2026-10-25 — User decision: one driver device per Windows virtual monitor
**Chosen:** OsplayHelper drives every virtual monitor with its own `Root\MttVDD` device, each configured for one monitor. It adds devices from the installed driver package when a session needs more than exist, enables as many as there are monitors and disables the rest.
**Alternatives:** One device with `<count>N</count>` (the previous design); building the driver from upstream source, which has the fix, and signing it ourselves (works only on machines that trust our certificate; shipping it needs a real code-signing certificate such as SignPath Foundation's); limiting the Windows host to one virtual monitor until upstream ships a signed fix.
**Why:** Two monitors on one device make the whole Windows desktop stutter, even with no capture running (measured on the RTX 3080 PC with OsplayVdTool, at 2 × 1080p as well as at 3420×2214 + 4K); one monitor does not. Upstream issues #140, #319 and #449 report the same. PR #482 fixes it (one swapchain worker for all monitors), but the latest signed release, 25.7.23, predates it. Separate devices avoid the shared worker and keep the signed driver. Issue #140 reports this workaround removing the lag on a similar Windows 11 + NVIDIA setup.
**Trade-offs:** The machine gets up to four root devices instead of one, and uninstall has to remove all of them. Every device reads the same settings file, so every monitor offers every session mode (the engine sets each one's own mode, as before).
**Revisit if:** A signed upstream release includes PR #482 and one device with several monitors runs smoothly.

## 2026-10-25 — User decision: several viewers share four virtual displays, one row per viewer
**Chosen:** A host extends onto several computers at once with at most four virtual displays in total; a Mac host still gives one viewer at most two. A new viewer gets the displays that are left, as many as it offers, and is refused as busy when none are. The Mac menu and the Windows panel show one row per connected computer (its name, how many displays it uses, its own Disconnect icon) and a row for the next computer the host waits for.
**Alternatives:** A limit per viewer only (rejected: the Windows driver cannot go past four devices' worth anyway, and a Mac host's encoder would run out); one summary card with a "disconnect all" button and the computers in a disclosure list.
**Why:** Four is what the Windows host can drive (helper protocol and driver devices) and about what one host's capture and encoder handle at 1080p. Rows keep every connection and its Disconnect one click away.
**Trade-offs:** A third viewer may get fewer displays than it offers, or none.
**Revisit if:** Hosts with more encoder capacity make more than four displays worthwhile.

## 2026-10-25 — Helper protocol 3: devices named by mask, one helper connection per engine
**Chosen:** Enable and Disable carry a mask of driver devices and Configure only writes the settings file, so one host session's monitors start and stop without restarting another's. All host sessions of the engine share one helper connection (`VirtualDisplayHub`); the helper keeps every device at the index it first got while the service runs, and the engine finds a session's new monitors as the ones that were not on the desktop before its devices started.
**Alternatives:** Keeping protocol 2 (Configure restarts every running device, so a second viewer would interrupt the first); one helper connection per session (the helper serves one client to stay simple and safe); naming devices by instance ID on the wire (more to validate, and the engine would need the helper's device list).
**Why:** Several viewers at once (SYM-03) need monitors that come and go independently. Instance IDs can fill gaps left by removed devices, so sorting them would move a running device to another index; a stable order avoids that.
**Trade-offs:** A device removed by hand while the service runs shifts the indices of the ones after it until the next session.
**Revisit if:** The helper ever serves more than one client, or a signed driver release makes one device with several monitors usable again.

## 2026-10-25 — Every driver device in its own host process
**Chosen:** OsplayHelper sets `HostProcessSharingEnabled` = 0 under each `Root\MttVDD` device's `Device Parameters\WUDF` key (the value the INF directive `UmdfHostProcessSharing = ProcessSharingDisabled` writes) before the device starts, so every device runs in a WUDFHost process of its own.
**Alternatives:** Listing a fixed set of common modes in every settings file (a viewer with another mode still fails); restarting every device when a viewer joins (interrupts the viewers already streaming); keeping one shared process.
**Why:** In the PC → two Macs test the second Mac's monitor could not take its 3360×2100 mode. UMDF pools a driver's devices into one host process, and the driver (25.7.23) appends each monitor's modes to a global list it never clears and reports the first entries, so a device started after another offers only the earlier device's modes. Upstream PR #482 clears that list, but no signed release has it.
**Trade-offs:** Up to four WUDFHost processes instead of one, a few megabytes each. The value is undocumented outside the INF directive; Windows reads it when the device starts.
**Revisit if:** A signed driver release clears its mode list per monitor.

## 2026-10-25 — Every driver device also gets a device group of its own
**Chosen:** Besides `HostProcessSharingEnabled` = 0, OsplayHelper writes `DeviceGroupId` = `Osplay.` plus the device's instance ID under `Device Parameters\WUDF`. A device it adds gets both values after `DiInstallDevice`, which applies them from the device's next start.
**Alternatives:** Restarting a new device after writing its values (stalled about 60 s, then UMDF reported the device offline after a driver crash); installing it disabled with `CONFIGFLAG_DISABLED` (the install started it anyway); writing the values before the install (the INF overwrites the group).
**Why:** The PC → two Macs test still failed with sharing disabled: the INF puts every device in `MttVDDGroup`, and devices of one group share a WUDFHost process, so the second monitor offered only 3420×2214. With a group per device, four devices ran in four processes and each monitor offered its own mode; two MacBooks then streamed at once.
**Trade-offs:** Devices added in one Enable request start together in the INF's group and share a process until their next start (issue #30).
**Revisit if:** A signed driver release clears its mode list per monitor, or issue #30 needs devices created before any session.

## 2026-10-25 — User decision: how releases are built and how users learn about updates
**Chosen:** Pushing a version tag makes CI build the Windows installer, unsigned, and attach it to a GitHub release. The macOS DMG is built, Developer ID signed, notarized (keychain profile `mac-notary`) and stapled on the maintainer's Mac by one script, which uploads it to the same release. Both apps check the latest GitHub release once a day and offer the download; no Sparkle. The README comes in English and Turkish and the repository gets a description and topics written for search.
**Alternatives:** Signing and notarizing the Mac app in CI with repository secrets and publishing a Sparkle appcast (the original Phase 14 plan); a Windows code-signing certificate (SignPath Foundation or paid); Sparkle for automatic installs on the Mac.
**Why:** The maintainer releases from a Mac, and the Windows build needs no Windows machine when CI makes it. No signing secret has to live in CI. A plain release check behaves the same on both platforms and needs no signing key or appcast.
**Trade-offs:** Windows shows SmartScreen once ("More info → Run anyway"); Mac users install updates by hand; every release needs the maintainer's Mac for the DMG.
**Revisit if:** A code-signing certificate becomes available, or users miss automatic updates.

## 2026-10-25 — What the Windows installer does on install, upgrade and uninstall
**Chosen:** An Inno Setup installer (admin, x64 Windows only) installs the self-contained app and the release-preset native binaries (static MSVC runtime), adds the bundled driver package with `pnputil /add-driver /install`, and runs `OsplayHelper.exe --install`, which adds the first `Root\MttVDD` device and registers or repoints the service. One inbound firewall rule allows `Osplay.exe` on private networks. An upgrade only stops the service (`--stop`) before replacing files and keeps the driver devices. Uninstall force-closes Osplay, runs `--uninstall` (service, devices, settings), asks before `--remove-driver` (silent uninstalls remove it) and deletes the rule, the Run entry and the helper's logs; paired devices and settings stay. Downloads carry no version in their names (`Osplay-Setup.exe`, `Osplay.dmg`) and each has a SHA-256 file.
**Alternatives:** devcon or the driver's own installer (more binaries to bundle, and devcon caused duplicate devices during development); removing and re-adding the devices on every upgrade; a framework-dependent app that needs the .NET Desktop Runtime; versioned asset names; Inno Setup 7's 64-bit setup.
**Why:** The helper already owns the devices, so installing and removing them in it keeps one implementation and no manual driver commands. Keeping devices across upgrades keeps the virtual monitors' layout in Windows. Self-contained and static builds need nothing else on a clean PC. Fixed asset names make `releases/latest/download/...` links in the README permanent. A 32-bit setup with `{sysnative}` compiles with both Inno Setup 6 and 7.
**Trade-offs:** The installer is about 60 MB larger than a framework-dependent one. Osplay has no way to be asked to quit, so install and uninstall close it forcibly. Another app that uses the same driver loses it if the user agrees to remove it. Browsers name a second download `Osplay-Setup (1).exe`.
**Revisit if:** Osplay gets a single-instance mutex (then use AppMutex), ARM64 Windows gets a driver, or a code-signing certificate becomes available.

## 2026-10-26 — User decision: the firewall rule covers every network profile
**Chosen:** The installer's inbound rule for `Osplay.exe` uses `profile=any`, and the Windows app no longer shows a Public-network hint (the panel banner and the Settings "Network" section are gone; the network category stays in the diagnostics report). Replaces "private networks" in the 2026-10-25 installer entry and DEVPLAN's "Private profile only".
**Alternatives:** Keeping the private-only rule and the hint that suggests switching the network to Private; asking at install time.
**Why:** Testing the 0.1.0 draft, a Mac could find the PC but every connection timed out: the PC's network was Public, so the private-only rule did not apply and Windows did not ask again because a rule for the program existed. The development build had worked on the same network because the user's answer to Windows' prompt covered Public. Windows 11 marks new networks Public, so most installs would have failed the same way.
**Trade-offs:** While Osplay runs, its listener accepts connections on public networks such as café Wi-Fi; only paired devices get past the Noise handshake, so the exposure is the handshake and protocol parsing, which are fuzzed.
**Revisit if:** A security review finds the pre-pairing surface too large for public networks, or users ask for a way to restrict Osplay to private networks.

## 2026-10-26 — User decision: tablets as viewers, then USB and Wi-Fi Direct
**Chosen:** A new Phase 15 adds an iPad viewer, then an Android tablet viewer, then USB connections (Android tethering, iPad through usbmuxd), then Wi-Fi Direct for PC ↔ PC and PC ↔ Android. Tablets are viewers only and forward no touch input.
**Alternatives:** Touch input forwarded as mouse events (Sidecar and spacedesk do it); Android first; Wi-Fi Direct before USB.
**Why:** iPadOS and Android give apps no system virtual displays, so a tablet can only be a display. The iPad reuses most of the Mac viewer, so it comes first. USB through tethering costs Android almost nothing, while the iPad needs a media transport without UDP. Apple has no Wi-Fi Direct for apps, so it can only join Windows and Android.
**Trade-offs:** Without touch input a tablet is a passive display. The iPad's USB path depends on Apple Devices being installed on Windows. Wi-Fi Direct shares the Wi-Fi card's channel with the router connection.
**Revisit if:** Users ask for touch input, or Apple opens a peer-to-peer Wi-Fi API.

## 2026-10-26 — Code shared by the Mac and iPad apps lives in macos/Shared
**Chosen:** The viewer pipeline, decoder, Bonjour discovery, key store, logging and the shared tunables move to `macos/Shared/`, which XcodeGen compiles into both the macOS and the iPad app targets. Platform parts (AppKit presenter, IOKit power assertion, local display list) stay in each app.
**Alternatives:** A Swift package target or framework shared by both apps; a full copy in the iPad app.
**Why:** A separate module would need almost every type made `public` and its own test target; compiling the same sources into both apps keeps internal access, the existing tests and the code unchanged. A copy would drift.
**Trade-offs:** The boundary is a folder, not enforced by the compiler; Mac-only code could creep into `Shared` (the iPad build catches it).
**Revisit if:** The shared code grows large enough to need its own tests on iOS, or a third Apple target appears.

## 2026-10-26 — Protocol 1.2: tablet platforms and the "pm" TXT key
**Chosen:** Platforms 3 (iPadOS) and 4 (Android) are added now, for both TAB-01 and TAB-02. The minor version becomes 2, and the Bonjour record gains a `pm` key with it. A record without `pm` comes from Osplay 0.1.
**Alternatives:** iPads announcing themselves as macOS (Osplay 0.1 would accept them, but the host would show and treat an iPad as a Mac); adding Android only in TAB-02 (a second protocol change); no `pm` key (an iPad would connect to a 0.1 host and fail with a closed connection).
**Why:** Osplay 0.1 validates the platform in Hello and in the TXT record and refuses unknown values, so tablets need a newer desktop app in any case. With `pm` an iPad can tell which computers need that update before it tries.
**Trade-offs:** iPads and Android tablets work only with Osplay 0.2 or later on the computer.
**Revisit if:** Another platform is added (it takes the next value and the next minor version).

## 2026-10-27 — Android app structure
**Chosen:** A Kotlin and Jetpack Compose app under `android/` with the C++ core built by the NDK through a small JNI bridge (`osplay_jni.cpp`). Discovery uses NsdManager, decoding uses MediaCodec in low-latency mode onto a SurfaceView, and the identity key is encrypted with an Android Keystore AES key. Services are wired in one composition root (`AppDependencies`) without a DI framework, and the view model sees only interfaces.
**Alternatives:** Hilt for injection (the Kotlin rules' default); a Kotlin port of the protocol; Flutter or React Native.
**Why:** One implementation of the protocol and crypto, as on every other platform. The app has one screen state and about ten services, so a hand-written root, like the Mac app's `AppDependencies`, is enough and keeps the build small. Text crosses JNI as UTF-8 bytes, because JNI's modified UTF-8 breaks names outside the BMP.
**Trade-offs:** Native code needs the NDK and CMake 3.31 in CI. The view model cannot be unit tested past the identity without the native library on the JVM.
**Revisit if:** The app grows several screens with their own state, or the JVM tests need the core.

## 2026-10-27 — iPad over USB: a usbmuxd stream link started from the iPad
**Chosen:** User decision: a cable session starts on the iPad. Its home screen has a "USB cable" row; tapping it makes the iPad listen on loopback port 41847, and a sharing Mac or PC on the cable connects through usbmuxd within seconds. Control frames and media datagrams share that stream as records (PROTOCOL.md "Stream links"). Neither side reconnects by itself after a disconnect. Android over USB stays IP tethering, which only Windows supports.
**Alternatives:** The computer connecting by itself whenever Osplay is open on an attached iPad; the computer listing "Apple device on USB" for its user to pick; negotiating the framing in Hello; the usbmuxd client in each platform app instead of the core.
**Why:** Only the computer can open a connection over the cable, and it cannot learn the iPad's name or whether Osplay waits before connecting. Starting on the iPad keeps the choice where it is on Wi-Fi, with the tablet's user, and tries that fail while the iPad is not waiting cost nothing visible. The connection itself says it is a stream link, so nothing needs negotiating. One usbmuxd client in the core serves macOS (its Unix socket) and Windows (the Apple Mobile Device Service on 127.0.0.1:27015).
**Trade-offs:** A sharing computer tries every attached iPad (and iPhone) every 2 s. TCP over the cable turns congestion into delay, bounded by dropping media beyond 1 MiB queued. A PC needs Apple Devices or iTunes installed. A Mac reaches an Android phone over the cable only through the phone's Wi-Fi hotspot.
**Revisit if:** Users expect plugging in to be enough, or a usbmuxd change refuses connections to app ports.

## 2026-10-27 — Without a router, the PC opens its own network
**Chosen:** User decision: Wi-Fi Direct in legacy mode. The PC becomes the group owner of a WPA2 network opened from the panel, and the other device joins it as an ordinary Wi-Fi network; discovery, pairing and sessions run over it unchanged. The name and password are generated once and kept, and the panel shows a standard Wi-Fi QR code. The shell gets the WinRT projections by targeting `net10.0-windows10.0.19041.0`, and QRCoder (MIT) draws the code. On Android, Osplay binds its sockets to such a Wi-Fi network while mobile data is the default route.
**Alternatives:** Real Wi-Fi Direct P2P between Windows and Android (WifiP2pManager) with discovery over its advertisement; both; Windows' Mobile hotspot (needs an internet connection to share).
**Why:** Every platform joins an ordinary network, Macs and iPads included, and nothing above the link changes. P2P would reach only Windows and Android, show Windows' pairing dialog on every connection and needs its own discovery; no documented Windows ↔ Android setup was found.
**Trade-offs:** The joining device has no internet while it is on the PC's network. The PC's Wi-Fi card must support Wi-Fi Direct, and Mobile hotspot stops it. Macs and iPads can join but not open such a network.
**Revisit if:** Users need their internet while connected, or Apple opens a peer-to-peer Wi-Fi API.
