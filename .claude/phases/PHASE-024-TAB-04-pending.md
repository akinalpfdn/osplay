# Phase 024 (TAB-04) — Wi-Fi Direct between PCs and Android tablets
Status: PENDING

DEVPLAN reference: Phase 15 (part TAB-04). Depends on: PHASE-023 (TAB-03).

## Goal
Two PCs, or a PC and an Android tablet, connect over Wi-Fi Direct when there is no router.

## Tasks
- [ ] Windows: advertise and connect with the WinRT Wi-Fi Direct API from the shell; the advertisement carries what Bonjour's TXT record carries.
- [ ] Android: Wi-Fi Direct with WifiP2pManager.
- [ ] Discovery over the Wi-Fi Direct advertisement where Bonjour does not reach the direct link; pairing over the direct link.
- [ ] UI: a "connect directly" choice when no router is available, on Windows and Android.
- [ ] Behaviour while the Wi-Fi card is also connected to a router (shared channel): measure and document.
- [ ] Docs: README, TROUBLESHOOTING, TESTING.

## Acceptance Criteria
- [ ] PC → PC over Wi-Fi Direct with neither PC connected to a router.
- [ ] PC → Android tablet over Wi-Fi Direct with no router.

## Decisions Made This Phase
(append as you go; full entries go to DECISIONS.md)
- Macs and iPads are out of scope: Apple gives apps no Wi-Fi Direct (AWDL is private).
