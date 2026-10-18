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

# Shared core and its Swift wrapper (CoreBridge) as a Swift package
swift test

# Menu bar app
cd macos
xcodegen generate
xcodebuild -project Osplay.xcodeproj -scheme Osplay -destination 'platform=macOS' \
  -derivedDataPath DerivedData test
open DerivedData/Build/Products/Debug/Osplay.app
```

The app has no Dock icon; it appears as a display icon in the menu bar.

If the clone lives in `~/Documents`, `~/Desktop` or `~/Downloads`, put the build folder elsewhere
(for example `-derivedDataPath ~/Library/Developer/Xcode/DerivedData/Osplay`): from those folders
video encoders take minutes to start (see `TROUBLESHOOTING.md`).

Debug builds show a **Host media test** in the menu bar popover: it creates one or two virtual
displays with a moving test pattern, captures and encodes them, streams them over loopback into
an in-process viewer and shows frame rate, frame interval (p99 and maximum), bitrate and latency
per stream. It needs Screen Recording permission. With "Save streams" on, the encoded streams are
written to `~/Movies/Osplay` as `.h264` files.

To work in Xcode, run `xcodegen generate` in `macos/` and open `Osplay.xcodeproj`. The project is
generated from `macos/project.yml` and is not committed: re-run `xcodegen generate` after pulling
changes or adding files.

### Protocol fixtures and fuzzing

`protocol/fixtures/` is generated from `docs/PROTOCOL.md` by an independent Python encoder:

```sh
python3 protocol/tools/generate_fixtures.py          # rewrite the fixtures after a protocol change
python3 protocol/tools/generate_fixtures.py --check  # what scripts/lint.sh runs
```

The C++ tests and the Swift tests (`swift test`) both read `protocol/fixtures/manifest.tsv`. Every
`ctest` run also replays the fixtures through the fuzz entry point. Real fuzzing needs Clang with
libFuzzer, which Apple's Clang lacks; CI runs it on Linux. Locally, with Homebrew's LLVM:

```sh
cmake -S core -B build/fuzz -G Ninja -DCMAKE_C_COMPILER="$(brew --prefix llvm)/bin/clang" \
  -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++" \
  -DOSPLAY_CORE_BUILD_FUZZER=ON -DOSPLAY_CORE_BUILD_TESTS=OFF
cmake --build build/fuzz
build/fuzz/fuzz/osplay_protocol_fuzzer -max_total_time=60 protocol/fixtures/control
build/fuzz/fuzz/osplay_crypto_fuzzer -max_total_time=60 protocol/fixtures/control
```

### Hardware tests (virtual displays)

`OsplayHardwareTests` drive the real window server: two virtual displays briefly appear while
they run, and windows may jump between screens. They are not part of CI or the default scheme.
Run them on a Mac after touching `macos/Osplay/Services/VirtualDisplay/` and after every macOS
update, because they use a private API:

```sh
cd macos
xcodebuild -project Osplay.xcodeproj -scheme OsplayHardware -destination 'platform=macOS' \
  -derivedDataPath DerivedData test
```

`HostPipelineHardwareTests` also captures and encodes two 2560×1440 at 60 Hz virtual displays for
ten seconds and expects at least 58 fps with a 99th-percentile frame interval under 33 ms. The test
host must have Screen Recording permission (grant it to the debug Osplay app once; a stable signing
identity keeps it, see below). The measured figures are written to
`$TMPDIR/osplay-host-pipeline.txt`. Build outside `~/Documents` (see `TROUBLESHOOTING.md`):

```sh
xcodebuild -project Osplay.xcodeproj -scheme OsplayHardware -destination 'platform=macOS' \
  -derivedDataPath ~/Library/Developer/Xcode/DerivedData/OsplayHardware test
cat "$TMPDIR/osplay-host-pipeline.txt"
```

`ViewerLoopbackHardwareTests` runs the whole loopback (host a 1920×1080 virtual display, view it
in a window) for ten seconds, expects at least 58 fps presented, then corrupts a frame and expects
recovery within one second; figures go to `$TMPDIR/osplay-viewer-loopback.txt`.

Debug builds also show a **Developer** section in the menu bar popover for creating one or two
test displays by hand, and a **Viewer loopback**: the Mac shows its own virtual display in a
window or full screen on the built-in display. ⌃⌥⌘V hides or shows the viewer.

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

### Portable Windows libraries

The Windows helper protocol, request handling and provider logic do not depend on Windows headers.
Build and test them on a Mac too:

```sh
cd windows
cmake --preset portable
cmake --build --preset portable
ctest --preset portable
```

### Formatting and lint

```sh
scripts/format.sh   # apply clang-format and swift-format in place
scripts/lint.sh     # check formatting, run clang-tidy, check translations
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

Build outputs land in `windows/build/<preset>/` for native code (executables and
`OsplayEngine.dll` in `windows/build/<preset>/bin/`) and in `bin/` folders for .NET.

### Virtual monitor (developer setup)

The Windows host's virtual monitor needs the Virtual Display Driver and the OsplayHelper service.
The installer will set both up from Phase 14 on; until then, once per machine:

1. Install the Virtual Display Driver. `winget install VirtualDrivers.Virtual-Display-Driver`
   only downloads and unpacks the package; it does **not** install the driver. Either:

   - download the **Virtual Driver Control** app from the
     [driver's releases](https://github.com/VirtualDrivers/Virtual-Display-Driver/releases), run
     it and click **Install**; or
   - from an **administrator** command line in the unpacked winget package, create the device with
     the bundled `devcon` (the folder is named `x86` but holds the x64 driver):

     ```powershell
     .\devcon.exe install SignedDrivers\x86\VDD\MttVDD.inf Root\MttVDD
     ```

   Windows adds an 800×600 monitor as soon as the driver is installed; OsplayHelper disables it
   again when it starts.

2. Install the helper service from the same administrator command line, after building:

   ```powershell
   cd windows\build\debug\bin
   .\OsplayHelper.exe --install
   ```

   The service points at this build folder. Run `.\OsplayHelper.exe --uninstall` before deleting
   or moving the folder, and reinstall after rebuilding the helper. Its log is
   `%ProgramData%\Osplay\logs\helper.log`.

3. Drive the monitor from a **normal** (non-administrator) command line with the developer tool:

   ```powershell
   cd windows\build\debug\bin
   .\OsplayVdTool.exe status
   .\OsplayVdTool.exe create 2560 1664 60
   ```

   `create` adds a monitor, sets exactly that mode and keeps it until you press Enter or close the
   tool. Closing the tool any other way (Ctrl+C, crash) removes the monitor too.

4. To check that the helper rejects malformed requests, send it garbage from PowerShell. .NET pipe
   clients connect anonymously by default, which the helper refuses, so request identification:

   ```powershell
   $p = [System.IO.Pipes.NamedPipeClientStream]::new('.', 'OsplayHelper',
       [System.IO.Pipes.PipeDirection]::InOut, [System.IO.Pipes.PipeOptions]::None,
       [System.Security.Principal.TokenImpersonationLevel]::Identification)
   $p.Connect(2000); $p.Write([byte[]](1,2,3,4), 0, 4); $p.Flush(); $p.Dispose()
   Get-Content "$env:ProgramData\Osplay\logs\helper.log" -Tail 3   # Rejected request: truncated
   ```

5. Test the host pipeline (capture, encode, cursor) from a normal command line. The helper must be
   running; the tool adds the virtual monitor itself:

   ```powershell
   cd windows\build\debug\bin
   .\OsplayHostTest.exe --seconds 60 --size 2560x1664 --hz 60 --dump $env:TEMP\osplay
   ```

   It connects an in-process viewer offering one monitor of that size, covers the virtual monitor
   with a moving pattern (`--no-pattern` to use your own content) and prints one line per second:
   frames per second, 99th-percentile and largest frame interval, bitrate, latency and cursor
   packets. With `--dump` the stream is saved as `.h264` (VLC, mpv or ffplay) and every cursor shape
   as `cursor-NNN.bmp`.

   Measured so far (Phase 8 acceptance: stable 60 fps at 2560×1664):

   | GPU | Encoder | Result |
   |-----|---------|--------|
   | NVIDIA (developer PC) | NVENC MFT | pending |
   | AMD | AMF MFT | not tested |
   | Intel | Quick Sync MFT | not tested |

> The driver's own documentation warns that major GPU or chipset driver updates can scramble
> display priority while it is installed. If the screen goes black after such an update, boot into
> Safe Mode and uninstall the Virtual Display Driver from Device Manager → Display adapters.

### Formatting

`dotnet format` covers C#. C++ and Swift formatting is checked by the macOS CI job; on Windows,
format a C++ file with the pinned version from `scripts/lint.sh`, for example
`uvx clang-format@23.1.3 -i windows\engine\src\capi\engine_capi.cpp`.

## Continuous integration

`.github/workflows/ci.yml` has two jobs:

- **macOS** — core CMake build and tests, portable Windows libraries and tests, `scripts/lint.sh`,
  `swift test`, app build and tests.
- **Protocol fuzzing** (Linux) — libFuzzer with ASan and UBSan, seeded with the fixtures: 30 seconds
  on the protocol decoders, 30 seconds on the handshake reader and media opener.
- **Windows** — core, engine, helper and developer tool with MSVC (`/W4 /WX`), all native tests,
  clang-tidy on core and Windows code, `dotnet format` check, shell build with analyzers as errors.
