# Building Osplay

Every command on this page is run by CI on each pull request. If a step here fails on a fresh
clone, that is a bug — please open an issue.

| Part | Built on | Toolchain |
|------|----------|-----------|
| `core/` | macOS and Windows | CMake ≥ 3.28, Ninja, Clang or MSVC (C++20) |
| `macos/` | macOS | Xcode 16 or later (Swift 6), XcodeGen |
| `windows/engine`, `windows/helper` | Windows | Visual Studio 2022 or later (MSVC), CMake, Ninja |
| `windows/app` | Windows | .NET 10 SDK |
| `windows/installer` | Windows | Inno Setup 6 or 7 |

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

### iPad app

The iPad viewer app is the `OsplayPad` scheme of the same project (iPadOS 18 or later). Its tests
run on any iPad simulator:

```sh
cd macos
xcodegen generate
xcodebuild -project Osplay.xcodeproj -scheme OsplayPad \
  -destination 'platform=iOS Simulator,name=iPad (A16)' -derivedDataPath DerivedData test
```

To run it on an iPad, connect the iPad, turn on Developer Mode on it (Settings › Privacy &
Security), choose the `OsplayPad` scheme and the iPad in Xcode, and run. With a manual signing
style in `Signing.local.xcconfig`, add a line so Xcode makes the iPad's development profile:

```
CODE_SIGN_STYLE[sdk=iphoneos*] = Automatic
```

The computer it connects to must run Osplay 0.2 or later (protocol 1.2), and the iPad asks once
for Local Network access.

### Release build

`scripts/macos/release.sh` builds the Release configuration, signs it with a Developer ID
certificate, notarizes and staples the app and `Osplay.dmg`, and adds the DMG to a GitHub release.
It is the maintainer's release step and needs their certificate; see [`RELEASING.md`](RELEASING.md).

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
scripts/lint.sh     # check formatting, run clang-tidy, check translations and UI tokens
```

Besides formatting and clang-tidy, `lint.sh` fails when a Mac view uses a literal spacing, size,
font or colour instead of a `Theme` token (`scripts/check_ui_tokens.sh`), when Swift code uses a
localization key that is not in the string catalog, and when any string lacks its English or
Turkish text (`scripts/check_localization.py`).

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
# Load the Visual Studio x64 compiler environment (also inside "Developer PowerShell for VS").
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
dotnet test Osplay.slnx -c Release --no-build
dotnet run --project Osplay.App -c Release
```

Build the native part first: the shell copies `OsplayEngine.dll` from `windows\build\debug\bin`
(another preset: `-p:OsplayNativeBin=<folder>`). The shell starts without a window and shows the
Osplay icon (two monitors) in the notification area: click it for the panel, right-click it for the
quick menu. Windows may hide new icons in the overflow menu (the `^` arrow on the taskbar).

Build outputs land in `windows/build/<preset>/` for native code (executables and
`OsplayEngine.dll` in `windows/build/<preset>/bin/`) and in `bin/` folders for .NET.

`Enter-DevShell.ps1` finds Visual Studio through the Visual Studio Installer. If the installer has
lost track of an installation that is still on disk, the script falls back to the newest
`vcvars64.bat` under `Program Files\Microsoft Visual Studio` and says so. It also clears the
`Platform=x64` variable the Visual Studio environment sets, which would otherwise make
`dotnet build` look for a `Release|x64` configuration that the solution does not have. Close the
running shell before rebuilding: it locks `OsplayEngine.dll` in its output folder.

### Installer

In a PowerShell command line at the repository root, with
[Inno Setup](https://jrsoftware.org/isinfo.php) 6 or 7 installed (`winget install JRSoftware.InnoSetup`):

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
. .\scripts\windows\Enter-DevShell.ps1
.\scripts\windows\Build-Installer.ps1
```

It writes `windows\build\installer\Osplay-Setup.exe` and `Osplay-Setup.exe.sha256`. On the way it
builds the native code with the `release` preset (static MSVC runtime, so no Visual C++
redistributable is needed) and runs its tests (`-SkipTests` skips them), publishes the shell
self-contained for win-x64 (no .NET runtime is needed), downloads the pinned Virtual Display Driver
release and checks its SHA-256, and stages the license texts. The version is `<Version>` in
`windows/app/Directory.Build.props`. The Release workflow (`.github/workflows/release.yml`) runs the
same script for version tags and for pull requests that change the packaging; see
[`RELEASING.md`](RELEASING.md).

The installer (`windows/installer/Osplay.iss`, English and Turkish) needs administrator rights and:

- installs the app, `OsplayEngine.dll` and `OsplayHelper.exe` under `Program Files\Osplay`;
- adds the Virtual Display Driver package to the driver store (`pnputil /add-driver`);
- runs `OsplayHelper.exe --install`, which adds the first driver device and registers and starts
  the OsplayHelper service;
- adds the inbound firewall rule "Osplay" for `Osplay.exe` on every network profile.

An upgrade stops the service first (`OsplayHelper.exe --stop`) and keeps the driver devices, so
Windows keeps the virtual monitors' arrangement. Uninstalling closes Osplay, runs
`OsplayHelper.exe --uninstall` (the service, every driver device and the driver settings), asks
whether to remove the driver package too (`--remove-driver`; a silent uninstall removes it), and
deletes the firewall rule, the "Start with Windows" entry and the helper's logs. Paired devices and
settings in `%APPDATA%` stay for a reinstall. The setup log is in `%TEMP%` (`Setup Log *.txt`).

### Virtual monitor (developer setup)

The Windows host's virtual monitor needs the Virtual Display Driver and the OsplayHelper service.
The installer sets both up, so the easiest start is to install a release once (or an installer
built as above). For a development build:

1. Point the service at your build from an **administrator** command line, after building:

   ```powershell
   cd windows\build\debug\bin
   .\OsplayHelper.exe --install
   ```

   `--install` adds the first driver device when there is none and registers the service, or
   repoints an existing one at this executable. Without an installed Osplay the driver package must
   be in the driver store first: unpack `VirtualDisplayDriver-x86.Driver.Only.zip` from the
   driver's [release 25.7.23](https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/tag/25.7.23)
   (the name says x86 but it holds the x64 driver) and run
   `pnputil /add-driver <folder>\VirtualDisplayDriver\MttVDD.inf /install`.

   Windows adds an 800×600 monitor as soon as a driver device exists; OsplayHelper disables it
   again when it starts. OsplayHelper adds a device per virtual monitor itself (two monitors on one
   device stall the desktop), and keeps the extra ones disabled.

   Reinstall after rebuilding the helper (the engine refuses a helper that speaks another protocol
   version with "helper not trusted/unavailable"); stop it with `sc.exe stop OsplayHelper` before
   the build replaces its executable. Running the installed copy's `--install` again
   (`"$env:ProgramFiles\Osplay\OsplayHelper.exe" --install`) points the service back at the
   release. `--uninstall` removes the service, the driver devices and the driver settings. The log
   is `%ProgramData%\Osplay\logs\helper.log`.

2. Drive the monitor from a **normal** (non-administrator) command line with the developer tool:

   ```powershell
   cd windows\build\debug\bin
   .\OsplayVdTool.exe status
   .\OsplayVdTool.exe create 2560 1664 60
   .\OsplayVdTool.exe create 3420 2214 60 3840 2160 60
   ```

   `create` adds one monitor per width, height and refresh rate (up to four), sets exactly those
   modes and keeps them until you press Enter or close the tool. Closing the tool any other way
   (Ctrl+C, crash) removes the monitors too. A last extra argument names the GPU.

3. To check that the helper rejects malformed requests, send it garbage from PowerShell. .NET pipe
   clients connect anonymously by default, which the helper refuses, so request identification:

   ```powershell
   $p = [System.IO.Pipes.NamedPipeClientStream]::new('.', 'OsplayHelper',
       [System.IO.Pipes.PipeDirection]::InOut, [System.IO.Pipes.PipeOptions]::None,
       [System.Security.Principal.TokenImpersonationLevel]::Identification)
   $p.Connect(2000); $p.Write([byte[]](1,2,3,4), 0, 4); $p.Flush(); $p.Dispose()
   Get-Content "$env:ProgramData\Osplay\logs\helper.log" -Tail 3   # Rejected request: truncated
   ```

4. Test the host pipeline (capture, encode, cursor) from a normal command line. The helper must be
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
   | NVIDIA (developer PC) | NVENC MFT | 60.0 fps, p99 frame interval 19–27 ms, max ≤ 30 ms; UAC, lock/unlock and orientation change recover; 25 cursor shapes correct |
   | AMD | AMF MFT | not tested |
   | Intel | Quick Sync MFT | not tested |

5. Test the viewer pipeline (decode, present, hide/show, unplug). `--list` shows the monitors the
   viewer offers; `--browse` lists the Osplay devices Bonjour finds; `--loopback` hosts a virtual
   monitor through the helper and views it on one physical monitor:

   ```powershell
   cd windows\build\debug\bin
   .\OsplayViewerTest.exe --list
   .\OsplayViewerTest.exe --browse --seconds 20
   .\OsplayViewerTest.exe --loopback --monitor 1 --seconds 60
   ```

   Every second the loopback prints, per stream, frames presented, decode errors and latency;
   Ctrl+Alt+Shift+V hides and shows the windows (the host pauses the streams while hidden).
   Viewing a Mac goes through the tray menu (step 6); with two monitors attached the Mac hosts two
   virtual displays, one per monitor, and unplugging one monitor stops only its stream.

   To confirm independent flip, start [PresentMon](https://github.com/GameTechDev/PresentMon) in an
   administrator terminal *before* the viewer, because the viewer windows cover every monitor:
   `PresentMon.exe --process_name OsplayViewerTest.exe --timed 60`. The `PresentMode` column must
   show `Hardware: Independent Flip` or `Hardware Composed: Independent Flip` (a multiplane overlay)
   for each window; composed flip is expected only for a moment at start and around hide/show.

   Streams run at most at 60 Hz, also on 120 Hz monitors (high refresh comes after the MVP). The
   Windows viewer draws no pointer: a Mac host puts it in the video, and in `--loopback` the
   Windows host sends it separately, so the loopback shows no pointer except while dragging.

   Measured so far (Phase 9 acceptance: two streams with independent flip):

   | Setup | Result |
   |-------|--------|
   | NVIDIA, VZ239 1080p60 + Acer 1440p120, Mac host (M3 Air) | independent flip on both (Acer through MPO), 0 decode errors, hide/show ≈ 1 s, unplug stops only that stream; 1080p60: 57–61 fps, latency 3–13 ms. The 1440p stream ran at 120 Hz then (80–99 fps, 40–65 ms) and is now capped at 60 Hz |
   | NVIDIA, loopback to VZ239 | 0 decode errors, 40–60 fps, latency 7–25 ms, first frame after ≈ 4 s |

6. Test both use cases end to end (DEVPLAN Phase 10). Run the WPF shell (`dotnet run --project
   Osplay.App -c Release`) on the PC and the Mac app on the Mac, on the same network:

   - **Use case 1 (the Mac gains the PC's monitors):** Mac menu → **Extend this Mac**; PC panel →
     **Use as display** → (optionally click a monitor in the picture to leave it out) → the Mac under
     **Nearby computers**. The first time, both computers show the same six-digit code; accept on
     both. The Mac gains one display per offered PC monitor; Ctrl+Alt+Shift+V (or the panel's **Hide
     or show the display**) hides and shows them on the PC.
   - **Use case 2 (Windows gains the MacBook's screen):** PC panel → **Extend this PC**; Mac menu →
     **Use as display** → the PC. The Windows virtual monitor appears full screen on the MacBook with
     the Windows pointer; ⌃⌥⌘V hides and shows it.
   - **Revoke:** Mac Settings › Devices, or PC Settings › Devices → **Revoke**. The other
     computer's next connection is refused ("refused this Mac/PC") without a pairing code;
     **Remove** lets it pair again.
   - **Robustness (Phase 13):** run the scenarios in `docs/TESTING.md`.
   - **Role swap (Phase 12b):** during use case 1 or 2 press ⌃⌥⌘S on the Mac or Ctrl+Alt+Shift+S
     on the PC (or **Swap roles**). Both sessions close and reconnect the other way round within a
     few seconds, without a pairing code. Note the time from the key press to the first frame.
     Stop the OsplayHelper service and swap from use case 1: the Mac reports that the PC cannot
     extend itself and the session continues.
   - **Shell checks (Phase 12):** switch Windows between light and dark (Settings › Personalization
     › Colors) with the panel and Settings open — both follow at once, and the tray icon follows the
     taskbar. Settings › Advanced shows the helper and driver as ready (stop the OsplayHelper
     service to see the repair message). Turn on the stats overlay and watch a Mac display: one line
     of figures in its top-left corner.

   The identity and paired devices live in the Keychain (items named "Osplay") on the Mac and in
   `%APPDATA%\Osplay` (DPAPI) on Windows; deleting them makes both sides pair again.

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
- **Windows** — core, engine, helper and developer tools with MSVC (`/W4 /WX`), all native tests,
  clang-tidy on core and Windows code, `dotnet format` check, shell build with analyzers as errors and
  shell tests (every string loads in English and Turkish).
