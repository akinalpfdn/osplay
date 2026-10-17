# Phase 015 (MVP-15) — Packaging, release and documentation
Status: PENDING

DEVPLAN reference: Phase 14. Depends on: Phase 13.

## Goal
Anyone can install Osplay from GitHub Releases, and contributors can build it from the docs alone.

## Tasks
- [ ] macOS: Developer ID signing, Hardened Runtime, notarization and stapling in CI using repository secrets; DMG; Sparkle appcast published with releases.
- [ ] Windows: Inno Setup installer that installs the shell, engine, helper service, VDD (per the Phase 0 decision) and firewall rules; clean uninstall that removes the service, disables and optionally removes VDD, and deletes firewall rules.
- [ ] Release workflow triggered by a version tag, producing both artifacts and release notes.
- [ ] Final `README.md`: what it is and is not, how it works (diagram), screenshots/GIF, requirements, installation per platform, first-run walkthrough, security model summary, performance expectations, troubleshooting links, building from source, contributing, license, credits (VDD, Monocypher, Asio, Sparkle, WPF-UI, H.NotifyIcon).
- [ ] Final `docs/` set brought up to date.

## Acceptance Criteria
- [ ] The DMG passes Gatekeeper on a clean Mac without warnings.
- [ ] The Windows installer works on a clean Windows 11 machine without manual steps beyond SmartScreen (until signing is in place).
- [ ] Uninstall leaves no service, driver config, firewall rule or virtual monitor behind.
- [ ] A new contributor can build both apps using only `BUILDING.md`.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
