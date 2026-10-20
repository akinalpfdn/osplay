# Phase 014 (MVP-14) — Robustness and diagnostics
Status: ACTIVE

DEVPLAN reference: Phase 13. Depends on: Phase 11, Phase 12.

## Goal
The system behaves predictably in everyday disruptions.

## Tasks
- [ ] Sleep/wake handling on both sides; network change handling; host display reconfiguration (resolution change on PC monitors, Mac lid close).
- [ ] Bitrate adaptation from receiver reports.
- [ ] Stats overlay on viewers (toggle via settings and hotkey).
- [ ] Structured logging: macOS unified logging; Windows rotating log files in `%LOCALAPPDATA%\Osplay\logs`; core logs routed into both. A "Copy diagnostics" action that bundles logs, versions and anonymized config for bug reports.
- [ ] Manual test matrix in `docs/TESTING.md`.

## Acceptance Criteria
- [ ] Each scenario in the test matrix passes on the reference hardware (M3 Air + the developer's PC).
- [ ] Waking either device restores the session automatically when both are awake and on the same network.
- [ ] A one-hour session shows no memory growth and no frame pacing degradation.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
