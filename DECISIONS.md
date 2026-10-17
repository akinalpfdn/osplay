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
