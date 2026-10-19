# Phase 012 (MVP-12) — macOS app UI
Status: ACTIVE

DEVPLAN reference: Phase 11. Depends on: Phase 10.

## Goal
A simple, clean, finished Mac interface.

## Tasks
- [x] Menu bar popover: discovered devices, paired devices, role switch (Share my screens / Use as display), connect/disconnect, current session status and quality preset.
- [x] Settings: General (launch at login via `SMAppService`, hotkeys), Displays (per-peer HiDPI choice), Devices (trusted list, revoke, rename), Advanced (stats overlay, logs folder).
- [x] Onboarding: permissions (Screen Recording, Local Network) with live status and deep links.
- [x] Pairing sheet showing the code with confirm/cancel.
- [x] Complete English and Turkish strings, theme tokens for every visual value, full VoiceOver labels.

## Acceptance Criteria
- [x] No hardcoded strings, colors, fonts or spacing in views (enforced by review checklist and a lint rule where possible).
- [ ] Switching the system language and appearance updates the UI correctly.
- [x] Every error code from `core` and services maps to a localized, actionable message.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision: native macOS look — system fonts and colours, SF Symbols, a Control Center–like menu bar window, one accent colour, follows light/dark automatically.
- User decision: hotkeys are recordable in Settings (click the field, press the new combination; conflicts are reported).
- User decision: hosting role named "Extend this Mac"; roles as cards; Settings and Quit as header icons; every caption wraps.
- Osplay arranges the virtual displays itself (per-device position): System Settings showed no Arrange button.
- User decision: the stats overlay is a small translucent box in a corner of the watched display, switched on in Settings.
