# Phase 011 (MVP-11) — Cross-platform interop, discovery and pairing
Status: PENDING

DEVPLAN reference: Phase 10. Depends on: Phase 7, Phase 9.

## Goal
Mac and Windows find each other, pair, and run both use cases end to end.

## Tasks
- [ ] Bonjour advertise/browse on macOS (Network.framework) and DNS-SD on Windows, feeding listener endpoints to `core`.
- [ ] Pairing flow surfaced through minimal temporary UI on both sides (final UI comes later).
- [ ] Keychain and DPAPI key stores.
- [ ] Viewer-driven negotiation: monitor offer, host creates matching virtual displays, stream configs, start.

## Acceptance Criteria
- [ ] Use case 1: the Mac gains two displays shown on the PC's two monitors.
- [ ] Use case 2: Windows gains one display shown on the MacBook's screen, with the Windows cursor visible.
- [ ] Unpaired devices cannot start streams; a revoked device is refused.
- [ ] Measured end-to-end latency on wired gigabit: median under 40 ms; on 5 GHz Wi-Fi: median under 60 ms. Method documented in `docs/TROUBLESHOOTING.md` (on-screen millisecond clock photographed together with the viewer).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
