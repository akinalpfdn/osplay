#!/usr/bin/env python3
"""Fails when any user-facing string lacks a complete English or Turkish translation.

Checks every String Catalog (*.xcstrings) under macos/ and every neutral .resx under windows/
against its Turkish satellite (<name>.tr.resx), and that every key the Mac app's Swift code uses
exists in its catalog.
"""

from __future__ import annotations

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REQUIRED_LANGUAGES = ("en", "tr")
CULTURE_SUFFIX = re.compile(r"\.[a-z]{2}(-[A-Za-z]{2,4})?\.resx$")
# A dotted key in a string literal, optionally followed by interpolations: "menu.quit", "a.b \\(x)".
SWIFT_KEY = re.compile(r'"([a-z][a-zA-Z]*(?:\.[a-zA-Z]+)+)((?:\s*\\\([^)"]*\))*)"')
PLACEHOLDER = re.compile(r"%(?:\d+\$)?(?:lld|ld|d|@|f|u)")


def unit_is_translated(localization: dict) -> bool:
    unit = localization.get("stringUnit")
    if unit is not None:
        return unit.get("state") == "translated" and bool(unit.get("value"))
    variations = localization.get("variations")
    if variations:
        return all(
            unit_is_translated(variant)
            for group in variations.values()
            for variant in group.values()
        )
    return False


def check_xcstrings(path: Path) -> list[str]:
    catalog = json.loads(path.read_text(encoding="utf-8"))
    problems = []
    for key, entry in catalog.get("strings", {}).items():
        if entry.get("shouldTranslate") is False:
            continue
        localizations = entry.get("localizations", {})
        for language in REQUIRED_LANGUAGES:
            if language not in localizations or not unit_is_translated(localizations[language]):
                problems.append(f"{path.relative_to(ROOT)}: '{key}' is not translated to '{language}'")
    return problems


def resx_entries(path: Path) -> dict[str, str]:
    tree = ET.parse(path)
    return {
        data.get("name", ""): (data.findtext("value") or "")
        for data in tree.getroot().iter("data")
        if data.get("type") is None
    }


def check_resx(neutral: Path) -> list[str]:
    turkish = neutral.with_name(neutral.name.replace(".resx", ".tr.resx"))
    if not turkish.exists():
        return [f"{neutral.relative_to(ROOT)}: missing Turkish file {turkish.name}"]
    source, target = resx_entries(neutral), resx_entries(turkish)
    problems = []
    for key in sorted(source.keys() - target.keys()):
        problems.append(f"{turkish.relative_to(ROOT)}: missing '{key}'")
    for key in sorted(target.keys() - source.keys()):
        problems.append(f"{turkish.relative_to(ROOT)}: '{key}' does not exist in {neutral.name}")
    for path, entries in ((neutral, source), (turkish, target)):
        for key, value in entries.items():
            if not value.strip():
                problems.append(f"{path.relative_to(ROOT)}: '{key}' is empty")
    return problems


def placeholder_count(key: str) -> int:
    return len(PLACEHOLDER.findall(key))


def check_swift_keys(catalog_path: Path, sources: list[Path]) -> list[str]:
    """Keys used in code but missing from the catalog. Only literals whose first segment is a
    catalog prefix count, so SF Symbol names and identifiers are not mistaken for keys."""
    keys = json.loads(catalog_path.read_text(encoding="utf-8")).get("strings", {}).keys()
    bases: dict[str, set[int]] = {}
    for key in keys:
        base = key.split(" ", 1)[0]
        bases.setdefault(base, set()).add(placeholder_count(key))
    prefixes = {base.split(".", 1)[0] for base in bases}
    problems = []
    for source in sources:
        for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), start=1):
            for match in SWIFT_KEY.finditer(line):
                base, interpolations = match.group(1), match.group(2)
                if base.split(".", 1)[0] not in prefixes:
                    continue
                arguments = interpolations.count("\\(")
                if arguments not in bases.get(base, set()):
                    problems.append(
                        f"{source.relative_to(ROOT)}:{number}: '{base}' with {arguments} argument(s) "
                        f"is not in {catalog_path.name}"
                    )
    return problems


def main() -> int:
    problems: list[str] = []
    catalogs = sorted((ROOT / "macos").rglob("*.xcstrings"))
    resources = sorted(
        path
        for path in (ROOT / "windows").rglob("*.resx")
        if not CULTURE_SUFFIX.search(path.name) and "bin" not in path.parts and "obj" not in path.parts
    )
    for catalog in catalogs:
        problems += check_xcstrings(catalog)
    app_catalog = ROOT / "macos/Osplay/Resources/Localizable.xcstrings"
    problems += check_swift_keys(app_catalog, sorted((ROOT / "macos/Osplay").rglob("*.swift")))
    for resource in resources:
        problems += check_resx(resource)

    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print(f"Localization complete: {len(catalogs)} string catalog(s), {len(resources)} resx file(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
