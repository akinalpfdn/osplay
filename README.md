<p align="right"><a href="README.tr.md">Türkçe</a></p>

<p align="center">
  <img src="artwork/icons/osplay-app-icon-1024.png" width="104" alt="Osplay app icon">
</p>

<h1 align="center">Osplay</h1>

<p align="center">
  <b>Turn a Mac or a Windows PC into a real extra monitor for another computer.</b><br>
  Over your local network · Mac ⇄ Windows in every direction · Free and open source
</p>

<p align="center">
  <a href="https://github.com/akinalpfdn/osplay/releases/latest/download/Osplay.dmg"><img src="artwork/readme/download-macos.svg" height="48" alt="Download for macOS"></a>&nbsp;&nbsp;
  <a href="https://github.com/akinalpfdn/osplay/releases/latest/download/Osplay-Setup.exe"><img src="artwork/readme/download-windows.svg" height="48" alt="Download for Windows"></a>
</p>

<p align="center">
  iPad as a display: <a href="https://testflight.apple.com/join/P19BeWpg">join the TestFlight beta</a>
</p>

<p align="center">
  <a href="https://github.com/akinalpfdn/osplay/releases/latest"><img src="https://img.shields.io/github/v/release/akinalpfdn/osplay?label=version&color=1d4ed8" alt="Latest version"></a>
  <img src="https://img.shields.io/badge/macOS-14%2B-111111" alt="macOS 14 or later">
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-0067c0" alt="Windows 10 or 11">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-2ea44f" alt="MIT license"></a>
</p>

<p align="center">
  <img src="artwork/readme/setup.jpg" width="100%" alt="A MacBook using both monitors of a Windows PC as its own displays">
  <br><sub>A MacBook using both monitors of a Windows PC as its own displays.</sub>
</p>

## See it in action

<p align="center">
  <img src="artwork/readme/demo.gif" width="100%" alt="A Windows PC extends onto two MacBooks at once, and a window moves from the PC's monitor onto a MacBook">
  <br><sub>One Windows PC extended onto two MacBooks at once: a window moves from the PC's monitor onto a MacBook.</sub>
</p>

## Features

- **Real displays, not a window.** macOS and Windows see an extra monitor: arrange it, drag windows onto it,
  set its resolution.
- **Every direction.** Mac → Windows, Windows → Mac, Mac → Mac and Windows → Windows.
- **All your monitors.** The computer used as a display lends every screen it has (up to four), and one
  computer can extend onto several others at once.
- **Local and private.** No cloud, no account. Devices pair once with a 6-digit code, and every
  connection is end-to-end encrypted.
- **Hardware video.** H.264 through VideoToolbox on the Mac and Media Foundation on Windows: 1080p by
  default, up to native resolution.
- **Swap roles in place.** One shortcut turns the display into the computer that extends.
- **English and Turkish**, light and dark themes.

## How Osplay compares

✅ yes · 🟡 partly or with extra setup · ❌ no

| | **Osplay** | Sidecar | Luna Display | Duet | spacedesk | Moonlight + Sunshine | Deskreen |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Price | **Free, open source** | Free, built into macOS | Hardware, one-time | Subscription | Free for personal use | Free, open source | Free, open source |
| A Windows PC as a Mac's display | ✅ | ❌ | ❌ | ✅ | ❌ | 🟡 | 🟡 |
| A Mac as a Windows PC's display | ✅ | ❌ | ❌ | ✅ | 🟡 | 🟡 | 🟡 |
| A real extra monitor, no dummy plug | ✅ | ✅ | ✅ | ✅ | ✅ | 🟡 | ❌ |
| Uses every monitor of the other computer | ✅ up to four | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Extends onto several computers at once | ✅ | ❌ | ❌ | 🟡 | ✅ | ❌ | ✅ |
| Swaps roles with one shortcut | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| iPad and Android tablets as displays | ✅ | iPad only | iPad only | ✅ | ✅ | ✅ | ✅ in a browser |
| Works without a router | ✅ USB or the PC's own network | ✅ | ✅ | ✅ | ✅ | 🟡 | ❌ |
| No account, no cloud | ✅ | Apple ID | ✅ | Account for wireless | ✅ | ✅ | ✅ |
| Touch, pen and input | ❌ by design | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| Over the internet | ❌ local only | ❌ | ❌ | ✅ | ❌ | ✅ | ❌ |

Osplay is built for one job: making a Mac and a Windows PC real monitors for each other, in every
direction and with every screen they have, for free. Another tool fits better if you need touch or
Apple Pencil on an iPad (Sidecar), pen pressure on an Android tablet (spacedesk, SuperDisplay), or a
display over the internet (Duet, Moonlight).

<sub>Compared in October 2026 from each product's site and published reviews; prices and features change.</sub>

## How it works

<p align="center">
  <img src="artwork/readme/how-it-works.svg" width="100%" alt="A window dragged from a PC's screen onto the MacBook next to it, over an encrypted local connection">
</p>

<img src="artwork/readme/mac-menu.png" width="280" align="right" alt="Osplay's menu on the Mac">

1. **Install** Osplay on both computers.
2. On the computer that needs more screen space, choose **Extend this Mac** (or **Extend this PC**).
3. On the other one, choose **Use as display** and click the first computer under **Nearby computers**.
4. The first time, confirm the same 6-digit code on both screens.

That's it: the new displays appear like plugged-in monitors. Arrange them in Osplay's Settings on the
Mac or in Windows' display settings.

Under the hood, the computer that extends gets virtual displays (CoreGraphics on the Mac, the
open-source [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) on
Windows), captures them, encodes them with the hardware H.264 encoder and streams them over an
encrypted connection. The other computer decodes them in hardware and shows each one full screen. One
shared C++ core handles discovery, pairing, encryption and the protocol for both apps; see
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

<br clear="right">

## Install

### macOS

1. Download [`Osplay.dmg`](https://github.com/akinalpfdn/osplay/releases/latest/download/Osplay.dmg),
   open it and drag Osplay to Applications.
2. Start Osplay; it lives in the menu bar.
3. Allow **Local Network** access so the two computers can find each other, and **Screen Recording**
   when you extend this Mac.

### Windows

1. Download [`Osplay-Setup.exe`](https://github.com/akinalpfdn/osplay/releases/latest/download/Osplay-Setup.exe)
   and run it.
2. The installer is not code-signed, so SmartScreen may say "Windows protected your PC": choose
   **More info → Run anyway**. Each release lists the installer's SHA-256
   ([how to check it](docs/TROUBLESHOOTING.md#windows-smartscreen-warns-about-the-installer)).
3. The installer adds the app, the Osplay helper service, the Virtual Display Driver and a firewall
   rule. Osplay lives in the notification area.

### Requirements

| | |
|---|---|
| **Mac** | macOS 14 Sonoma or later, Apple silicon (Intel Macs run it too, untested) |
| **Windows** | Windows 10 22H2 or Windows 11, x64, a GPU with hardware H.264 encoding and decoding |
| **Network** | Both computers on the same local network; 5 GHz Wi-Fi works, a cable helps with several large displays |

## Keyboard shortcuts

| Action | Mac | Windows |
|--------|-----|---------|
| Hide or show the displays (on the computer used as a display) | ⌃⌥⌘V | Ctrl+Alt+Shift+V |
| Swap roles | ⌃⌥⌘S | Ctrl+Alt+Shift+S |
| Statistics overlay | ⌃⌥⌘I | Ctrl+Alt+Shift+I |

## FAQ

<details>
<summary><b>Can I use my MacBook as a second monitor for my Windows PC?</b></summary>

Yes. Choose **Use as display** on the MacBook and **Extend this PC** on the PC.
</details>

<details>
<summary><b>Can I use a Windows laptop as an external monitor for my Mac?</b></summary>

Yes, the same way in the other direction. Mac to Mac and PC to PC work too.
</details>

<details>
<summary><b>Can one computer use several others as displays?</b></summary>

Yes. A computer can extend onto several computers at once, with up to four extra displays in total.
</details>

<details>
<summary><b>Does it need the internet or an account?</b></summary>

No. Everything stays on your local network, and there is no account or cloud service.
</details>

<details>
<summary><b>Can I control the other computer with my mouse?</b></summary>

No. Osplay extends the screen only; it is not a remote desktop tool. Each computer keeps its own
keyboard and mouse.
</details>

<details>
<summary><b>Why does a streaming service show a black screen?</b></summary>

Protected video (DRM) is not captured by the operating system, so it appears black on the extra display.
</details>

<details>
<summary><b>The picture stutters on Wi-Fi. What can I do?</b></summary>

Lower **Stream size** (next to Quality on the computer that extends): 1080p suits most networks. On a
Mac, AirDrop and other Apple wireless features can interrupt Wi-Fi in short bursts; see
[Troubleshooting](docs/TROUBLESHOOTING.md).
</details>

<details>
<summary><b>Is it free?</b></summary>

Yes. Osplay is open source under the MIT license.
</details>

## More

- **Security:** devices pair once by confirming a 6-digit code on both screens; connections use the
  Noise protocol (X25519, ChaCha20-Poly1305, BLAKE2b). See [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md),
  and [`SECURITY.md`](SECURITY.md) to report a vulnerability.
- **Troubleshooting:** [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md). Settings › Advanced ›
  Diagnostics copies a report for bug reports, with device names and addresses hidden.
- **Building from source:** [`docs/BUILDING.md`](docs/BUILDING.md).
- **Contributing:** issues and pull requests are welcome; see [`CONTRIBUTING.md`](CONTRIBUTING.md) and the
  [Code of Conduct](CODE_OF_CONDUCT.md).

## License and credits

[MIT](LICENSE) © 2026 Akinalp Fidan. Osplay builds on
[Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver),
[Monocypher](https://monocypher.org), [Asio](https://think-async.com/Asio/),
[WPF-UI](https://github.com/lepoco/wpfui) and [H.NotifyIcon](https://github.com/HavenDV/H.NotifyIcon);
full list in [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).
