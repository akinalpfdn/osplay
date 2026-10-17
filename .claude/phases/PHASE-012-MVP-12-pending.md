# Phase 012 (MVP-12) — macOS app UI
Status: PENDING

DEVPLAN reference: Phase 11. Depends on: Phase 10.

## Goal
A simple, clean, finished Mac interface.

## Tasks
- [ ] Menu bar popover: discovered devices, paired devices, role switch (Share my screens / Use as display), connect/disconnect, current session status and quality preset.
- [ ] Settings: General (launch at login via `SMAppService`, hotkeys), Displays (per-peer HiDPI choice), Devices (trusted list, revoke, rename), Advanced (stats overlay, logs folder).
- [ ] Onboarding: permissions (Screen Recording, Local Network) with live status and deep links.
- [ ] Pairing sheet showing the code with confirm/cancel.
- [ ] Complete English and Turkish strings, theme tokens for every visual value, full VoiceOver labels.

## Acceptance Criteria
- [ ] No hardcoded strings, colors, fonts or spacing in views (enforced by review checklist and a lint rule where possible).
- [ ] Switching the system language and appearance updates the UI correctly.
- [ ] Every error code from `core` and services maps to a localized, actionable message.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
