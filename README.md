# Osplay

[![CI](https://github.com/akinalpfdn/osplay/actions/workflows/ci.yml/badge.svg)](https://github.com/akinalpfdn/osplay/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Osplay turns one computer's screen into a real, OS-level extended display for another computer on
the same local network. No HDMI, no remote control, no cloud.

> **Status:** early development — nothing is usable yet. Progress follows the phases in
> [`DEVPLAN.md`](DEVPLAN.md).

## What it is — and is not

- **Mac → Windows:** your Mac gains up to two extra displays that macOS treats as real monitors;
  their content appears full screen on the monitors of a Windows PC.
- **Windows → Mac:** Windows gains one extra display, shown full screen on the MacBook's screen.
- **Not remote desktop.** The viewer never sends keyboard or mouse input; each computer is
  controlled with its own input devices.

## How it works

The host creates virtual displays, captures them, encodes them with hardware H.264 and streams
them over an encrypted LAN connection. The viewer decodes and presents them full screen with
minimal latency. Details: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Screenshots

Coming with the first release.

## Requirements

- **macOS:** macOS 14 Sonoma or later, Apple Silicon.
- **Windows:** Windows 10 22H2 or Windows 11, x64, GPU with hardware H.264 encode and decode.
- Both devices on the same local network; wired gigabit recommended, 5 GHz Wi-Fi supported.

## Installation

### macOS

Coming with the first release (notarized DMG from GitHub Releases).

### Windows

Coming with the first release (installer from GitHub Releases).

## First run

Coming with the first release.

## Security

Devices pair once by confirming a 6-digit code on both screens; all traffic is end-to-end
encrypted. See [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) and [`SECURITY.md`](SECURITY.md).

## Performance expectations

Targets: median end-to-end latency under 40 ms on wired gigabit and under 60 ms on 5 GHz Wi-Fi.
Measured numbers will be published once the pipeline is complete.

## Troubleshooting

See [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md).

## Building from source

See [`docs/BUILDING.md`](docs/BUILDING.md).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

[MIT](LICENSE) © 2026 Akinalp Fidan.

## Credits

Osplay builds on [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver),
[Monocypher](https://monocypher.org), [Asio](https://think-async.com/Asio/),
[Sparkle](https://sparkle-project.org), [WPF-UI](https://github.com/lepoco/wpfui) and
[H.NotifyIcon](https://github.com/HavenDV/H.NotifyIcon). Full list:
[`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).
