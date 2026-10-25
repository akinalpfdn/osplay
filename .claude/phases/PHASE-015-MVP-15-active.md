# Phase 015 (MVP-15) — Packaging, release and documentation
Status: ACTIVE

DEVPLAN reference: Phase 14. Depends on: Phase 13.

## Goal
Anyone can install Osplay from GitHub Releases, and contributors can build it from the docs alone.

## Tasks
- [ ] macOS: Developer ID signing, Hardened Runtime, notarization (keychain profile `mac-notary`) and stapling on the maintainer's Mac with one release script that uploads the DMG to the tag's release.
- [x] macOS: a daily check against the latest GitHub release with a download link, like the Windows shell (no Sparkle).
- [ ] Windows: Inno Setup installer, built by the release workflow and unsigned, that installs the shell, engine, helper service, VDD (per the Phase 0 decision) and firewall rules; clean uninstall that removes the service, disables and optionally removes VDD (every `Root\MttVDD` device, including the ones OsplayHelper added — one per virtual monitor, DECISIONS.md 2026-10-25), and deletes firewall rules.
- [ ] Release workflow triggered by a version tag, producing both artifacts and release notes.
- [ ] Final `README.md` and Turkish `README.tr.md`: what it is and is not, how it works (diagram), screenshots/GIF, requirements, installation per platform, first-run walkthrough, security model summary, performance expectations, troubleshooting links, building from source, contributing, license, credits (VDD, Monocypher, Asio, WPF-UI, H.NotifyIcon).
- [x] Repository description and topics written for search.
- [ ] Final `docs/` set brought up to date.

## Acceptance Criteria
- [ ] The DMG passes Gatekeeper on a clean Mac without warnings.
- [ ] The Windows installer works on a clean Windows 11 machine without manual steps beyond SmartScreen (until signing is in place).
- [ ] Uninstall leaves no service, driver config, firewall rule or virtual monitor behind.
- [ ] A new contributor can build both apps using only `BUILDING.md`.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-25 — User decision: the Windows installer comes from CI on a version tag, unsigned; the macOS DMG is signed and notarized on the maintainer's Mac and uploaded by one script; both apps check the latest GitHub release (no Sparkle). README in English and Turkish, written for search.
