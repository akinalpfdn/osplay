# Phase 001 (MVP-01) — Repository scaffolding
Status: ACTIVE

DEVPLAN reference: Phase 0. Depends on: Nothing.

## Goal
A clean, empty-but-building monorepo with all hygiene and tooling in place.

## Tasks
- [ ] Directory layout as described in Platform & Stack.
- [ ] `README.md` skeleton (sections listed in Phase 14), `LICENSE` (MIT), `CONTRIBUTING.md`, `SECURITY.md` (private vulnerability reporting via GitHub Security Advisories), `CODE_OF_CONDUCT.md`, issue and PR templates.
- [ ] `.gitignore` covering Xcode, SwiftPM, CMake build dirs, Visual Studio, .NET, Inno output; `.gitattributes` with line-ending rules; `.editorconfig`.
- [ ] Formatting and linting: `.clang-format`, `.clang-tidy`, swift-format config, .NET analyzers with `Directory.Build.props` enabling nullable and warnings-as-errors.
- [ ] `core/` CMake project with an empty library and one passing test; `Package.swift` exposing `core` to SwiftPM.
- [ ] `macos/project.yml` generating an empty menu bar app that links `core` and launches.
- [ ] `windows/` CMake presets for engine and helper (empty targets), a WPF shell solution that launches to a tray icon.
- [ ] GitHub Actions: macOS job (build core + app, run tests), Windows job (build core, engine, helper, shell, run tests).
- [ ] `docs/` with placeholder `ARCHITECTURE.md`, `PROTOCOL.md`, `BUILDING.md`, `TROUBLESHOOTING.md`, `SECURITY_MODEL.md`.
- [ ] Third-party license review note in `docs/THIRD_PARTY.md`, including the VDD redistribution decision.

## Acceptance Criteria
- [ ] Fresh clone builds on macOS (terminal) and Windows (command line) by following `BUILDING.md` exactly.
- [ ] Both CI jobs are green.
- [ ] Formatters and linters run in CI and fail on violations.
- [ ] VDD license compatibility recorded with the chosen distribution approach.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
