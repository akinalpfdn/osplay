# Phase 024 (TAB-04) — A PC's own Wi-Fi network for connections without a router
Status: ACTIVE

DEVPLAN reference: Phase 15 (part TAB-04). Depends on: PHASE-023 (TAB-03).

## Goal
A PC opens its own Wi-Fi network from Osplay (Wi-Fi Direct legacy mode), and another PC, an Android device, a Mac or an iPad joins it, so Osplay works where there is no router.

## Tasks
- [ ] Windows: open and close the network from the shell with the WinRT Wi-Fi Direct publisher in legacy mode (SSID `Osplay-…`, generated passphrase kept in settings so joined devices rejoin); report when the Wi-Fi card cannot.
- [ ] Windows UI: a "No router?" choice in the panel that shows the network name, password and a standard Wi-Fi QR code (phone and tablet cameras join from it).
- [ ] Sessions, Bonjour and the firewall over the PC's network: the PC as host and as viewer; fix what does not reach that interface.
- [ ] Android: a Wi-Fi network without internet next to mobile data — bind Osplay's traffic to the Wi-Fi network so discovery and sessions use it.
- [ ] Mac and iPad: joining from the Wi-Fi menu works unchanged; the apps' "no computers" hint mentions it.
- [ ] Behaviour while the PC's Wi-Fi card is also connected to a router (shared channel): measure and document.
- [ ] Docs: TROUBLESHOOTING, TESTING, ARCHITECTURE; README once tested.

## Acceptance Criteria
- [ ] PC → PC and PC ← PC on the PC's network, neither on a router.
- [ ] PC → Android on the PC's network, Android joined from the QR code, mobile data on.
- [ ] Mac ↔ PC on the PC's network.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- 2026-10-27 — User decision: the PC opens its own network (Wi-Fi Direct legacy mode) instead of real Wi-Fi Direct P2P pairing. Every platform can join it as a normal Wi-Fi network, so Macs and iPads are no longer out of scope as joiners; a Mac or iPad cannot open such a network itself. The joining device leaves its router while joined.
