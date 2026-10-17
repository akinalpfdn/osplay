# Phase 001 (MVP-01) — Repository scaffolding
Status: ACTIVE

DEVPLAN reference: Phase 0. Depends on: Nothing.

## Goal
A clean, empty-but-building monorepo with all hygiene and tooling in place.

## Tasks
- [x] Directory layout as described in Platform & Stack.
- [x] `README.md` skeleton (sections listed in Phase 14), `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` (private vulnerability reporting via GitHub Security Advisories), `CODE_OF_CONDUCT.md`, issue and PR templates.
- [x] `.gitignore` covering Xcode, SwiftPM, CMake build dirs, Visual Studio, .NET, Inno output; `.gitattributes` with line-ending rules; `.editorconfig`.
- [x] Formatting and linting: `.clang-format`, `.clang-tidy`, swift-format config, .NET analyzers with `Directory.Build.props` enabling nullable and warnings-as-errors.
- [x] `core/` CMake project with an empty library and one passing test; `Package.swift` exposing `core` to SwiftPM.
- [x] `macos/project.yml` generating an empty menu bar app that links `core` and launches.
- [x] `windows/` CMake presets for engine and helper (empty targets), a WPF shell solution that launches to a tray icon.
- [x] GitHub Actions: macOS job (build core + app, run tests), Windows job (build core, engine, helper, shell, run tests).
- [x] `docs/` with placeholder `ARCHITECTURE.md`, `PROTOCOL.md`, `BUILDING.md`, `TROUBLESHOOTING.md`, `SECURITY_MODEL.md`.
- [x] Third-party license review note in `docs/THIRD_PARTY.md`, including the VDD redistribution decision.

## Acceptance Criteria
- [ ] Fresh clone builds on macOS (terminal) and Windows (command line) by following `BUILDING.md` exactly.
  - macOS: verified on the M3 Air (2026-10-17).
  - Windows: CI runs the same commands (VS 2026, MSVC); still needs one manual run on the developer PC. User decision (2026-10-17): treat as passing for now and run it later.
- [x] Both CI jobs are green (PR #1, run 39027035895).
- [x] Formatters and linters run in CI and fail on violations (each checker shown to fail on an injected violation).
- [x] VDD license compatibility recorded with the chosen distribution approach.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- VDD is MIT → bundle pinned signed release (DECISIONS.md, THIRD_PARTY.md).
- Lint tools pinned via uvx (clang-format 23.1.3, clang-tidy 22.1.8); swift-format from Xcode.
- macOS signs ad hoc by default; maintainer team goes in gitignored Signing.local.xcconfig.
- Windows native build: Ninja + MSVC through vswhere/Enter-VsDevShell (works with VS 2022 and 2026).
- WPF strings live in a separate Osplay.Localization assembly: WPF markup pass 1 cannot see resx accessors generated in the same project.
- WPF-UI is deferred to Phase 12 (first phase that uses it); Phase 0 shell only needs H.NotifyIcon + CommunityToolkit.Mvvm.
- clang-tidy runs on core only; engine/helper get clang-tidy on Windows once they contain real Windows code (Phase 2).
- Fresh-clone macOS steps of BUILDING.md verified on the M3 Air; Windows steps verified only through CI.
- main and v* tags protected by rulesets; only akinalpfdn (admin bypass) can push, merge or force push.
