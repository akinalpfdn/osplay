# Building Osplay

Every command on this page is run by CI on each pull request. If a step here fails on a fresh
clone, that is a bug — please open an issue.

| Part | Built on | Toolchain |
|------|----------|-----------|
| `core/` | macOS and Windows | CMake ≥ 3.28, Ninja, Clang or MSVC (C++20) |
| `macos/` | macOS | Xcode 16 or later (Swift 6), XcodeGen |
| `windows/engine`, `windows/helper` | Windows | Visual Studio 2022 or later (MSVC), CMake, Ninja |
| `windows/app` | Windows | .NET 10 SDK |

## macOS

### Requirements

- macOS 14 Sonoma or later. Apple Silicon is the supported architecture.
- Xcode 16 or later, opened once so it finishes installing components.
- [Homebrew](https://brew.sh).

Install the build tools in a terminal:

```sh
brew install cmake ninja xcodegen uv
```

### Build and test

In a terminal:

```sh
git clone https://github.com/akinalpfdn/osplay.git
cd osplay

# Shared core (CMake build + GoogleTest)
cd core
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cd ..

# Shared core as a Swift package (Swift tests)
swift test

# Menu bar app
cd macos
xcodegen generate
xcodebuild -project Osplay.xcodeproj -scheme Osplay -destination 'platform=macOS' \
  -derivedDataPath DerivedData test
open DerivedData/Build/Products/Debug/Osplay.app
```

The app has no Dock icon; it appears as a display icon in the menu bar.

To work in Xcode, run `xcodegen generate` in `macos/` and open `Osplay.xcodeproj`. The project is
generated from `macos/project.yml` and is not committed: re-run `xcodegen generate` after pulling
changes or adding files.

### Code signing for local development

The committed configuration signs ad hoc, so any clone builds without an Apple account. macOS
binds privacy permissions such as Screen Recording to the code signature, and ad hoc signatures
change on every build, so regular development is easier with a stable identity. Create
`macos/Config/Signing.local.xcconfig` (it is gitignored):

```
CODE_SIGN_STYLE = Automatic
CODE_SIGN_IDENTITY = Apple Development
DEVELOPMENT_TEAM = <your team ID>
```

Then run `xcodegen generate` again.

### Formatting and lint

```sh
scripts/format.sh   # apply clang-format and swift-format in place
scripts/lint.sh     # check formatting, run clang-tidy on core, check translations
```

`scripts/lint.sh` is exactly what CI runs. Tool versions are pinned inside it and fetched with `uv`
on first use.

## Windows

### Requirements

- Windows 10 22H2 or Windows 11, x64.
- [Visual Studio 2022 or later](https://visualstudio.microsoft.com/) with the
  **Desktop development with C++** workload. It includes MSVC, CMake and Ninja.
- [.NET 10 SDK](https://dotnet.microsoft.com/download/dotnet/10.0).
- [Git for Windows](https://git-scm.com/download/win).

### Build and test

In a PowerShell command line:

```powershell
git clone https://github.com/akinalpfdn/osplay.git
cd osplay

# Allow the repository's helper script for this PowerShell session only.
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
# Load the Visual Studio x64 compiler environment (skip this inside "Developer PowerShell for VS").
. .\scripts\windows\Enter-DevShell.ps1

# Native engine, helper service and core tests
cd windows
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# WPF shell
cd app
dotnet format Osplay.slnx --verify-no-changes
dotnet build Osplay.slnx -c Release
dotnet run --project Osplay.App -c Release
```

The shell starts without a window and shows an "O" icon in the notification area; right-click it
to quit. Windows may hide new icons in the overflow menu (the `^` arrow on the taskbar).

Build outputs land in `windows/build/<preset>/` for native code and in `bin/` folders for .NET.

### Formatting

`dotnet format` covers C#. C++ and Swift formatting is checked by the macOS CI job; on Windows,
format a C++ file with the pinned version from `scripts/lint.sh`, for example
`uvx clang-format@23.1.3 -i windows\engine\src\capi\engine_capi.cpp`.

## Continuous integration

`.github/workflows/ci.yml` has two jobs:

- **macOS** — core CMake build and tests, `scripts/lint.sh`, `swift test`, app build and tests.
- **Windows** — core, engine and helper with MSVC (`/W4 /WX`) and core tests, `dotnet format`
  check, shell build with analyzers as errors.
