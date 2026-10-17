# Architecture

> Status: outline. Sections are filled in by the phases that build each part; the full design is in
> [`DEVPLAN.md`](../DEVPLAN.md) → Architecture.

## Overview

Every installation can act as a **host** (owns virtual displays, captures, encodes, sends) or a
**viewer** (receives, decodes, presents full screen). One session connects one host to one viewer
and carries one video stream per virtual display.

```
HOST                                                         VIEWER
Virtual display ─► Capture ─► Encoder ─► core: packetize ──UDP──► core: reassemble ─► Decoder ─► Presenter
                                              ▲                        │
                                              └──── TCP control ◄──────┘
```

## Repository layout

| Path | Contents |
|------|----------|
| `core/` | Shared C++20 library: protocol, crypto, transport, media, session, flat C API (`core/include/osplay/osplay.h`) |
| `Package.swift` | Exposes `core` to Swift as the `OsplayCore` module |
| `macos/` | Swift menu bar app, generated with XcodeGen from `macos/project.yml` |
| `windows/engine/` | Native engine DLL (`OsplayEngine.dll`): capture, encode, decode, present, discovery |
| `windows/helper/` | `OsplayHelper` Windows service for privileged virtual display driver operations |
| `windows/app/` | WPF shell (`Osplay.App`) and its string resources (`Osplay.Localization`) |
| `windows/installer/` | Inno Setup script |
| `protocol/` | Binary fixtures shared by every test suite |
| `docs/` | This documentation |

## Build graph

- `core` is a static library. CMake (`core/CMakeLists.txt`) is the reference build; SwiftPM
  (`Package.swift`) compiles the same sources for the macOS app.
- The macOS app links `core` through the local Swift package.
- `windows/CMakeLists.txt` adds `core` as a subdirectory and links it into the engine DLL.
- The WPF shell loads the engine DLL through P/Invoke (from Phase 12).

## Virtual Display Driver control surface (Windows)

To be written in Phase 2: configuration schema, named pipe commands, and which changes need a
device restart.
