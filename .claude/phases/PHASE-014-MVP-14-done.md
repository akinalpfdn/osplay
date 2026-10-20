# Phase 014 (MVP-14) — Robustness and diagnostics
Status: DONE

DEVPLAN reference: Phase 13. Depends on: Phase 11, Phase 12.

## Goal
The system behaves predictably in everyday disruptions.

## Tasks
- [x] Sleep/wake handling on both sides; network change handling; host display reconfiguration (resolution change on PC monitors, Mac lid close).
- [x] Bitrate adaptation from receiver reports.
- [x] Stats overlay on viewers (toggle via settings and hotkey).
- [x] Structured logging: macOS unified logging; Windows rotating log files in `%LOCALAPPDATA%\Osplay\logs`; core logs routed into both. A "Copy diagnostics" action that bundles logs, versions and anonymized config for bug reports.
- [x] Manual test matrix in `docs/TESTING.md`.

## Acceptance Criteria
- [x] Each scenario in the test matrix passes on the reference hardware (M3 Air + the developer's PC).
- [x] Waking either device restores the session automatically when both are awake and on the same network.
- [x] A one-hour session shows no memory growth and no frame pacing degradation.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Bitrate adaptation in the core (loss and resends, AIMD between a quarter of and the configured bitrate).
- Interruptions end the session; the viewer reconnects to the same host by device ID (sleep sends Bye 3 → PEER_SLEEPING).
- A viewer monitor mode change restarts the session instead of renegotiating.
- Stats overlay shortcut ⌃⌥⌘I / Ctrl+Alt+Shift+I; Copy diagnostics on both apps (names and addresses replaced).
- Accepted on the reference hardware 2026-10-20: every scenario in docs/TESTING.md passed, including the one-hour session.
