# Third-party components

Licenses were reviewed on 2026-10-17. Each component is re-checked when it is first added to the
build (the phase is listed) and whenever its pinned version changes.

| Component | Used for | License | Distributed with Osplay | Added in |
|-----------|----------|---------|-------------------------|----------|
| [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) (MttVDD) | Windows virtual monitor | MIT | Yes — bundled signed binaries | Phase 2 (dev), Phase 14 (installer) |
| [Monocypher](https://monocypher.org) 4 | X25519, ChaCha20-Poly1305, BLAKE2b | BSD-2-Clause or CC0-1.0 | Yes — vendored source | Phase 4 |
| [Asio](https://think-async.com/Asio/) (standalone) | Sockets and timers in `core` | BSL-1.0 | Yes — vendored headers | Phase 5 |
| [Sparkle](https://sparkle-project.org) 2 | macOS auto-updates | MIT | Yes — framework in the app | Phase 14 |
| [WPF-UI](https://github.com/lepoco/wpfui) | Fluent controls, theme following | MIT | Yes — NuGet assembly | Phase 12 |
| [H.NotifyIcon.Wpf](https://github.com/HavenDV/H.NotifyIcon) 2.4.1 | Windows tray icon | MIT | Yes — NuGet assembly | Phase 0 |
| [CommunityToolkit.Mvvm](https://github.com/CommunityToolkit/dotnet) 8.4.2 | WPF view models | MIT | Yes — NuGet assembly | Phase 0 |
| [GoogleTest](https://github.com/google/googletest) 1.18.0 | C++ tests | BSD-3-Clause | No — build time only | Phase 0 |
| [XcodeGen](https://github.com/yonaskolb/XcodeGen) | Xcode project generation | MIT | No — build tool | Phase 0 |
| [Inno Setup](https://jrsoftware.org/isinfo.php) | Windows installer | Inno Setup License | Installer runtime only | Phase 14 |

Release packages must include each distributed component's copyright and license text: the macOS
app in its "Acknowledgements" resource, the Windows installer on its license page and in the
install folder.

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

**Conditions to re-check before Phase 14:** that the signed driver package itself carries no
additional terms (from the signing provider) that forbid redistribution, and which release is
pinned. Full rationale: `DECISIONS.md`, entry "Virtual Display Driver distribution".
