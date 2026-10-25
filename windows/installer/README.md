# Windows installer

`Osplay.iss` is the Inno Setup script that installs the shell, engine, helper service, Virtual
Display Driver and firewall rule, and removes all of them on uninstall. `Osplay.en.isl` and
`Osplay.tr.isl` hold its own messages (UTF-8 with a byte order mark, which Inno Setup needs);
`scripts/check_localization.py` checks that both have the same ones. `licenses/` holds the license
texts of the distributed components that are not in the source tree.

Build it with `scripts/windows/Build-Installer.ps1`, which stages the files the script expects; see
[`docs/BUILDING.md`](../../docs/BUILDING.md#installer) for what the installer does and
[`docs/THIRD_PARTY.md`](../../docs/THIRD_PARTY.md) for the driver distribution decision.
