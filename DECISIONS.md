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
