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

