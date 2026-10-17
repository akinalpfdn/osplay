# Security Policy

Osplay streams your screen over the local network and installs a privileged Windows service, so
security reports are taken seriously.

## Supported versions

Only the latest release receives security fixes. Osplay has not had a release yet.

## Reporting a vulnerability

**Do not open a public issue.** Report privately through GitHub Security Advisories:

1. Open <https://github.com/akinalpfdn/osplay/security/advisories/new>
   (or the repository's **Security** tab → **Report a vulnerability**).
2. Describe the issue, the affected platform and version, and steps to reproduce.
3. If you have a proof of concept, attach it to the advisory, not to a public place.

The report stays private between you and the maintainer until a fix is published. You will get a
reply in the advisory thread; please allow time for a fix before any disclosure.

## In scope

- The Noise handshake, pairing code, key storage and transport encryption in `core/`.
- Parsing of any data received from the network (control messages, media and cursor packets).
- The `OsplayHelper` Windows service and its named pipe (privilege escalation, command injection).
- The installers and the update mechanism (Sparkle on macOS, update check on Windows).

## Out of scope

- Attacks that require an already compromised host or viewer machine.
- Content protected by DRM appearing black (this is expected behavior).
- Denial of service from a device on the same LAN flooding traffic.

See [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md) for the threat model.
