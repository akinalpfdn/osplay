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

## Virtual displays on macOS

`VirtualDisplayProviding` (`macos/Osplay/Services/VirtualDisplay/`) is the only way the app creates
displays. Its implementation, `CGVirtualDisplayProvider`, is the only code that touches the private
`CGVirtualDisplay` API declared in `CGVirtualDisplayPrivate.h`. Resolution listing and switching
use public CoreGraphics API (`DisplayModes`).

Behavior measured on a MacBook Air M3, macOS 26.6, lid open (Phase 1):

| Observation | Consequence |
|-------------|-------------|
| Two virtual displays run at the same time next to the built-in screen; the one-external-display hardware limit does not apply. | Use case 1 is feasible. |
| Releasing the `CGVirtualDisplay` object removes the display immediately; macOS moves its windows. | Display lifetime = handle lifetime. |
| Every mode `W×H` passed in the settings yields a 1× resolution and, with `hiDPI = 1` and a `2W×2H` backing within `maxPixels`, a 2× "looks like W×H" resolution. macOS adds its own standard sizes. | Modes are "looks like" sizes: native, ¾ and ½ of the remote monitor. `maxPixels` is twice the native size when HiDPI is on. |
| On a display identity it has not seen, macOS picks the largest 2× mode: a 2560×1440 monitor starts at a 5120×2880 backing. | Osplay activates the "crisp" HiDPI resolution (looks like half size, backing = remote pixels) when it wants a default, instead of rendering and scaling four times the pixels. |
| Vendor, product and serial number identify the display; a recreated display with the same identity returns to its previous position and resolution. | Identity is derived from the peer ID and remote monitor index (FNV-1a, never serial 0). |
| `applySettings` on a live display changes its modes without changing its display ID. | Remote monitor changes do not recreate the display. |

## Virtual Display Driver control surface (Windows)

To be written in Phase 2: configuration schema, named pipe commands, and which changes need a
device restart.
