# Osplay

## What This Is
Osplay turns one computer's screen into a real, OS-level extended display for another computer on
the same LAN. Mac host → Windows viewer (two virtual displays on the Mac, shown on the PC's two
monitors) and Windows host → Mac viewer (one virtual display on Windows, shown on the MacBook).
Not remote desktop: no input is forwarded. Open source (MIT), no backend services.

`DEVPLAN.md` is the authoritative plan: stack, constraints, protocol decisions and phases.

## Tech Stack
| Layer | Technology | Why |
|-------|-----------|-----|
| Shared core | C++20, CMake + SwiftPM, flat C API | One implementation of the wire protocol for both apps |
| macOS app | Swift 6 (strict concurrency), SwiftUI menu bar, XcodeGen | Native APIs: CGVirtualDisplay, ScreenCaptureKit, VideoToolbox |
| Windows engine/helper | C++20, CMake presets (Ninja + MSVC) | DXGI Desktop Duplication, Media Foundation, VDD control |
| Windows shell | C# .NET 10, WPF, WPF-UI, CommunityToolkit.Mvvm | Tray app and settings; calls the engine via P/Invoke |
| Persistence | Keychain / DPAPI, UserDefaults / JSON | No server, no database |

## Architecture
See `DEVPLAN.md` → Architecture and `docs/ARCHITECTURE.md`.
`core/` (protocol, crypto, transport, media, session, capi) is used by `macos/` (Swift) and
`windows/engine/` (C++ DLL). `windows/helper/` is a privileged service for the virtual display
driver. `windows/app/` is the WPF shell.

## Current Phase
See `.claude/phases/` — always check the active phase file before starting work.
Phase files map DEVPLAN "Phase N" to `PHASE-0(N+1)-MVP-(N+1)`.

## Rules
claude-ground rules live in `~/.claude/rules/` and load automatically — no imports needed here.
- `common/` rules load in every session.
- Language rules and `frontend.md` load when Claude reads a matching file (`paths:` frontmatter).

## Project-Specific Constraints
- One branch per DEVPLAN phase, merged by the user through a PR after CI is green.
- Identifier prefix: `com.akinalpfdn.osplay`.
- Every user-facing string is localized (English + Turkish); `core` and the engine return error codes, never text.
- Every visual value comes from the platform theme source.
- Run `scripts/lint.sh` before committing; CI fails on formatting and lint violations.
- Builds and checks are described in `docs/BUILDING.md` — keep it exact; it is an acceptance criterion.

## Context
- Developer hardware: MacBook Air M3 (macOS 26) and a Windows PC with two monitors.
- Mac developer can only verify Windows code through CI (`windows` job).
