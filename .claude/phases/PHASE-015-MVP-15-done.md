# Phase 015 (MVP-15) — Packaging, release and documentation
Status: DONE — v0.1.1 released 2026-10-26

DEVPLAN reference: Phase 14. Depends on: Phase 13.

## Goal
Anyone can install Osplay from GitHub Releases, and contributors can build it from the docs alone.

## Tasks
- [x] macOS: Developer ID signing, Hardened Runtime, notarization (keychain profile `mac-notary`) and stapling on the maintainer's Mac with one release script that uploads the DMG to the tag's release.
- [x] macOS: a daily check against the latest GitHub release with a download link, like the Windows shell (no Sparkle).
- [x] Windows: Inno Setup installer, built by the release workflow and unsigned, that installs the shell, engine, helper service, VDD (per the Phase 0 decision) and firewall rules; clean uninstall that removes the service, disables and optionally removes VDD (every `Root\MttVDD` device, including the ones OsplayHelper added — one per virtual monitor, DECISIONS.md 2026-10-25), and deletes firewall rules.
- [x] Release workflow triggered by a version tag, producing both artifacts and release notes.
- [x] Final `README.md` and Turkish `README.tr.md`: what it is and is not, how it works (diagram), screenshots/GIF, requirements, installation per platform, first-run walkthrough, security model summary, performance expectations, troubleshooting links, building from source, contributing, license, credits (VDD, Monocypher, Asio, WPF-UI, H.NotifyIcon).
- [x] Repository description and topics written for search.
- [x] Final `docs/` set brought up to date (RELEASING, BUILDING, THIRD_PARTY, TROUBLESHOOTING, ARCHITECTURE).

## Acceptance Criteria
- [x] The DMG passes Gatekeeper on a clean Mac without warnings. (2026-10-26: app and DMG notarized and stapled; `spctl` reports "Notarized Developer ID" for both.)
- [x] The Windows installer works on a clean Windows 11 machine without manual steps beyond SmartScreen (until signing is in place). (2026-10-26: 0.1.1 installed on both Windows PCs; connects on a Public network after the firewall fix, PR #33.)
- [x] Uninstall leaves no service, driver config, firewall rule or virtual monitor behind. (2026-10-26, second Windows PC: no service, rule, driver package or files left.)
- [x] A new contributor can build both apps using only `BUILDING.md`. (CI runs the BUILDING.md commands on every pull request; not tried by a new person.)

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-25 — User decision: the Windows installer comes from CI on a version tag, unsigned; the macOS DMG is signed and notarized on the maintainer's Mac and uploaded by one script; both apps check the latest GitHub release (no Sparkle). README in English and Turkish, written for search.
- 2026-10-25 — The installer adds the driver with pnputil and lets OsplayHelper add and remove the devices (`--install`, `--stop`, `--uninstall`, `--remove-driver`); upgrades keep the devices; downloads have fixed names (`Osplay-Setup.exe`, `Osplay.dmg`) and SHA-256 files. Release steps: `docs/RELEASING.md`.
- 2026-10-26 — User decision: the installer's firewall rule covers every network profile and the Public-network hint is gone (a private-only rule dropped every connection on the user's Public network during the 0.1.0 draft test).
- 2026-10-26 — Windows runs one Osplay per session; a second start opens the running one's panel (PR #35). The installer keeps closing Osplay itself instead of using AppMutex.
