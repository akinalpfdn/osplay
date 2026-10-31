# Third-party components

Licenses were reviewed on 2026-10-17. Each component is re-checked when it is first added to the
build (the phase is listed) and whenever its pinned version changes.

| Component | Used for | License | Distributed with Osplay | Added in |
|-----------|----------|---------|-------------------------|----------|
| [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) (MttVDD) 25.7.23 | Windows virtual monitor | MIT | Yes — bundled signed binaries | Phase 2 (dev), Phase 14 (installer) |
| [UsbDk](https://github.com/daynix/UsbDk) 1.0.22 | Switching Android devices on the USB cable into accessory mode (Windows) | Apache-2.0 | Yes — bundled signed MSI, installed only when absent | TAB-05 |
| [Monocypher](https://monocypher.org) 4.0.3 | X25519, ChaCha20-Poly1305, BLAKE2b | BSD-2-Clause or CC0-1.0 | Yes — vendored source (`core/third_party/monocypher`) | Phase 4 |
| [cacophony](https://github.com/haskell-cryptography/cacophony) Noise test vectors | Handshake tests | Unlicense | No — test data | Phase 4 |
| [noise-c](https://github.com/rweather/noise-c) Noise test vectors | Handshake tests | MIT | No — test data | Phase 4 |
| [Asio](https://think-async.com/Asio/) 1.38.2 (standalone) | Sockets and timers in `core` | BSL-1.0 | Yes — vendored headers (`core/third_party/asio`) | Phase 5 |
| [WPF-UI](https://github.com/lepoco/wpfui) 4.3.0 | Fluent controls, theme following | MIT | Yes — NuGet assembly | Phase 12 |
| [.NET](https://github.com/dotnet/runtime) 10 runtime and Windows Desktop | Runtime of the self-contained Windows shell | MIT, with its own third-party notices | Yes — in the installer | Phase 14 |
| [H.NotifyIcon.Wpf](https://github.com/HavenDV/H.NotifyIcon) 2.4.1 | Windows tray icon | MIT | Yes — NuGet assembly | Phase 0 |
| [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet) 8.4.2 | WPF view models | MIT | Yes — NuGet assembly | Phase 0 |
| [QRCoder](https://github.com/Shane32/QRCoder) 1.6.0 | QR code for joining the PC's own network | MIT | Yes — NuGet assembly | Phase 15 |
| [GoogleTest](https://github.com/google/googletest) 1.18.0 | C++ tests | BSD-3-Clause | No — build time only | Phase 0 |
| [XcodeGen](https://github.com/yonaskolb/XcodeGen) | Xcode project generation | MIT | No — build tool | Phase 0 |
| [Inno Setup](https://jrsoftware.org/isinfo.php) | Windows installer | Inno Setup License | Installer runtime only | Phase 14 |

Where the license texts ship:

- **Windows installer:** its license page shows Osplay's license, and `licenses\` in the install
  folder holds Osplay's license and the texts of every distributed component above: the files in
  `windows/installer/licenses/` (Virtual Display Driver, UsbDk, WPF-UI, H.NotifyIcon, CommunityToolkit, QRCoder),
  the vendored Monocypher and Asio licenses, and the .NET SDK's `LICENSE.txt` and
  `ThirdPartyNotices.txt`. `scripts/windows/Build-Installer.ps1` stages them.
- **macOS app:** it contains only Monocypher and Asio, and neither requires a notice in a binary:
  Osplay uses Monocypher under its CC0-1.0 option, and BSL-1.0 exempts copies in machine-executable
  form. The source tree keeps both license files.

## Virtual Display Driver redistribution

**Decision:** bundle the official signed VDD release in the Windows installer, pinned to an exact
release, rather than downloading it at install time or installing it through `winget`.

**Basis:** the repository is MIT licensed ("Copyright (c) 2024 Virtual Display", verified through
the GitHub license API on 2026-10-17). MIT permits copying and distributing the software, including
binaries, provided the copyright notice and permission notice are included. Bundling therefore
only obliges Osplay to ship that notice, which the installer does.

**Why bundle:** installs work offline, are reproducible (a known driver version is tested with
each Osplay release) and do not break if upstream renames release assets — upstream asset names
have already changed between releases (`VirtualDisplayDriver-x64.zip` in 24.10.27 vs
`VirtualDisplayDriver-x86.Driver.Only.zip` in 25.7.23).

**Checked in Phase 14 (2026-10-25):** the pinned release is 25.7.23,
`VirtualDisplayDriver-x86.Driver.Only.zip` (SHA-256
`e24210692b442b39af763536330ce78b423f19342b7a7792c26de3944e418b3a`; the name says x86 but it holds
the x64 driver). The archive holds only the driver files and a sample settings file, no further
terms. Its catalog is signed by SignPath Foundation, whose
[terms](https://signpath.org/terms) bind the signing project and put no condition on redistributors.
The installer ships the MIT notice in `licenses\VirtualDisplayDriver-LICENSE.txt`. Full rationale:
`DECISIONS.md`, entry "Virtual Display Driver distribution".

## UsbDk redistribution

**Decision:** bundle the official signed UsbDk 1.0.22 MSI from the
[v1.00-22 release](https://github.com/daynix/UsbDk/releases/tag/v1.00-22)
(`UsbDk_1.0.22_x64.msi`, SHA-256
`91f6f695e1e13c656024e6d3b55620bf08d8835ef05ee0496935ba6bb62466a5`, checked on 2026-10-30), for
the same reasons as the Virtual Display Driver. The installer runs it silently only when no UsbDk
is installed, and the uninstaller removes it only when Osplay installed it, since other apps
(Spice, QEMU USB redirection) use the same driver.

**Basis:** UsbDk is Apache-2.0 (Red Hat and Daynix). Apache-2.0 permits redistributing binaries
with a copy of the license and any `NOTICE` file; the tagged source has no `NOTICE`. The installer
ships the license in `licenses\UsbDk-LICENSE.txt`. Osplay calls `UsbDkHelper.dll` at run time and
mirrors two of its structure layouts (`windows/engine/src/usb/usbdk.cpp`); no UsbDk source is
copied.

## Android accessory driver

The installer binds Windows' own WinUSB driver to Android devices in accessory mode
(`USB\VID_18D1&PID_2D00`) with `windows/installer/accessory/OsplayAccessory.inf`, Osplay's own
file. The INF names only WinUSB, which ships with Windows, so nothing third-party is distributed.
Its catalog is made and signed on each PC by `AccessoryDriver.ps1` with PowerShell's
`New-FileCatalog` and a self-signed certificate whose private key is deleted after signing — the
method [Zadig](https://zadig.akeo.ie)'s libwdi uses, written from the Windows documentation; no
libwdi (LGPL) code is used. Decision: `DECISIONS.md`, 2026-10-30.
