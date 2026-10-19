# Phase 011 (MVP-11) — Cross-platform interop, discovery and pairing
Status: DONE

DEVPLAN reference: Phase 10. Depends on: Phase 7, Phase 9.

## Goal
Mac and Windows find each other, pair, and run both use cases end to end.

## Tasks
- [x] Bonjour advertise/browse on macOS (Network.framework) and DNS-SD on Windows, feeding listener endpoints to `core`.
- [x] Pairing flow surfaced through minimal temporary UI on both sides (final UI comes later).
- [x] Keychain and DPAPI key stores.
- [x] Viewer-driven negotiation: monitor offer, host creates matching virtual displays, stream configs, start.

## Acceptance Criteria
- [x] Use case 1: the Mac gains two displays shown on the PC's two monitors.
- [x] Use case 2: Windows gains one display shown on the MacBook's screen, with the Windows cursor visible.
- [x] Unpaired devices cannot start streams; a revoked device is refused.
- [ ] Measured end-to-end latency on wired gigabit: median under 40 ms; on 5 GHz Wi-Fi: median under 60 ms. Method documented in `docs/TROUBLESHOOTING.md` (on-screen millisecond clock photographed together with the viewer).

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- User decision: Windows temporary UI is the WPF tray menu (discovered devices, role, connect, pairing dialog) with the real P/Invoke layer, reused in Phase 12.
- Mac temporary UI: a "Devices" section in the menu bar window, replaced in Phase 11.
- Bonjour on macOS through the DNS-SD C API, not Network.framework (DECISIONS.md).
- Windows engine keeps the identity and answers trust itself; revoked devices are kept and refused.
- Latency acceptance (camera method) not measured yet; carried as an open item.
- Phase 9's temporary IP connection (Mac debug remote host, `OsplayViewerTest --connect`) is removed.
