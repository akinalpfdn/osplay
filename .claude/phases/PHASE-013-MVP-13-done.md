# Phase 013 (MVP-13) — Windows app UI
Status: DONE

DEVPLAN reference: Phase 12. Depends on: Phase 10.

## Goal
A simple, clean, finished Windows interface.

## Tasks
- [x] Tray icon with menu: status, connect/disconnect, hide/show viewer, open settings, quit.
- [x] Main window (Fluent, WPF-UI): devices, role switch, monitor selection for lending (with a visual layout of the PC's monitors), quality preset.
- [x] Settings: start with Windows, hotkeys, trusted devices, stats overlay, logs folder, update check.
- [x] Pairing dialog, network profile warning, VDD/helper health check with repair guidance.
- [x] P/Invoke layer with callback marshaling to the dispatcher.
- [x] Complete English and Turkish `.resx`, theme dictionaries following the system theme.

## Acceptance Criteria
- [x] No hardcoded strings or visual values in XAML or code-behind.
- [x] System light/dark switch is reflected live.
- [x] Engine crashes or helper unavailability are surfaced clearly, and the shell never hangs on a native call (all native calls off the UI thread).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision: Mac-like shell — a tray flyout panel (role cards, devices, monitors, quality) plus a separate Settings window; no navigation-pane main window.
- User decision: the viewer hide/show hotkey is recordable (engine takes the hotkey in the viewer config).
- Main window = the tray panel (user decision above); monitor selection is a picture of the PC's monitors in the panel.
- User decision: the stats overlay ships in this phase, drawn by the engine on the stream.
- User decision: update check once a day at launch plus a manual button; can be turned off.
- Accepted on the PC 2026-10-20; an in-process engine crash cannot be caught — a missing or damaged engine and engine error codes are what the shell surfaces.
