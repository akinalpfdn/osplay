# Phase 013 (MVP-13) — Windows app UI
Status: PENDING

DEVPLAN reference: Phase 12. Depends on: Phase 10.

## Goal
A simple, clean, finished Windows interface.

## Tasks
- [ ] Tray icon with menu: status, connect/disconnect, hide/show viewer, open settings, quit.
- [ ] Main window (Fluent, WPF-UI): devices, role switch, monitor selection for lending (with a visual layout of the PC's monitors), quality preset.
- [ ] Settings: start with Windows, hotkeys, trusted devices, stats overlay, logs folder, update check.
- [ ] Pairing dialog, network profile warning, VDD/helper health check with repair guidance.
- [ ] P/Invoke layer with callback marshaling to the dispatcher.
- [ ] Complete English and Turkish `.resx`, theme dictionaries following the system theme.

## Acceptance Criteria
- [ ] No hardcoded strings or visual values in XAML or code-behind.
- [ ] System light/dark switch is reflected live.
- [ ] Engine crashes or helper unavailability are surfaced clearly, and the shell never hangs on a native call (all native calls off the UI thread).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
