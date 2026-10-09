#!/usr/bin/env python3
"""License gate and acknowledgements check for the libraries PlatformIO resolved.

Commands:
  check [--env tbeam] [--libdeps DIR] [--readme README.md]
        reads every library in the env's libdeps folder (run a build first) and fails if
        - its license is unknown, or copyleft and not in COPYLEFT_ALLOWLIST
        - it is missing from the README "# Acknowledgements" section, or listed there with a
          different license family

The license comes from library.json or library.properties, else from the LICENSE file text.
The framework (ESP-IDF, Arduino-ESP32) is not in libdeps; the README lists it by hand.
Standard library only.
"""
import argparse
import json
import re
import sys
from pathlib import Path
from typing import List, NamedTuple, Optional

from release import ROOT, workspace_dir

PERMISSIVE_FAMILIES = {"MIT", "BSD", "Apache", "ISC", "Zlib", "BSL"}
# LGPL libraries accepted on 2026-10-06; LoRaChat publishes its MIT source with each release.
COPYLEFT_ALLOWLIST = {"TinyGPSPlus", "EspSoftwareSerial", "ghostl"}
LICENSE_FILE = re.compile(r"^(licen[cs]e|copying)(\.(txt|md))?$", re.IGNORECASE)
README_SECTION = "# Acknowledgements"


class Library(NamedTuple):
    name: str
    version: str
    license: Optional[str]
    url: str


def detect_license(text: str) -> Optional[str]:
    """@return the SPDX-like license id of a LICENSE text, or None if not recognised."""
    upper = text.upper()
    version = re.search(r"VERSION (\d)(?:\.(\d))?", upper)
    number = f"{version.group(1)}.{version.group(2) or 0}" if version else ""
    if "GNU AFFERO GENERAL PUBLIC" in upper:
        return f"AGPL-{number}" if number else "AGPL"
    if "GNU LESSER GENERAL PUBLIC" in upper or "GNU LIBRARY GENERAL PUBLIC" in upper:
        return f"LGPL-{number}" if number else "LGPL"
    if "GNU GENERAL PUBLIC LICENSE" in upper:
        return f"GPL-{number}" if number else "GPL"
    if "APACHE LICENSE" in upper:
        return "Apache-2.0" if "VERSION 2.0" in upper else "Apache"
    if "MIT LICENSE" in upper or "PERMISSION IS HEREBY GRANTED, FREE OF CHARGE" in upper:
        return "MIT"
    if "BSD" in upper or "REDISTRIBUTION AND USE IN SOURCE AND BINARY FORMS" in upper:
        return "BSD"
    return None


def license_family(license_id: str) -> str:
    return re.split(r"[-\s]", license_id, maxsplit=1)[0]


def normalize_url(url: str) -> str:
    url = url.strip().rstrip("/")
    return url[:-4] if url.endswith(".git") else url


def read_properties(path: Path) -> dict:
    fields = {}
    for line in path.read_text(errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            fields[key.strip()] = value.strip()
    return fields


def read_library(lib_dir: Path) -> Optional[Library]:
    if (lib_dir / "library.json").is_file():
        meta = json.loads((lib_dir / "library.json").read_text(errors="replace"))
        repository = meta.get("repository")
        url = repository.get("url", "") if isinstance(repository, dict) else repository or ""
        url = url or meta.get("homepage", "")
    elif (lib_dir / "library.properties").is_file():
        meta = read_properties(lib_dir / "library.properties")
        url = meta.get("url", "")
    else:
        return None
    license_id = meta.get("license")
    if not license_id:
        for path in sorted(lib_dir.iterdir()):
            if path.is_file() and LICENSE_FILE.match(path.name):
                license_id = detect_license(path.read_text(errors="replace"))
                if license_id:
                    break
    return Library(meta.get("name") or lib_dir.name, str(meta.get("version", "")), license_id,
                   normalize_url(url))


def scan(libdeps: Path) -> List[Library]:
    """@return the libraries in a PlatformIO libdeps/<env> folder, ordered by name."""
    libs = (read_library(path) for path in sorted(libdeps.iterdir()) if path.is_dir())
    return [lib for lib in libs if lib is not None]


def check_licenses(libs: List[Library]) -> List[str]:
    errors = []
    for lib in libs:
        if not lib.license:
            errors.append(f"{lib.name}: unknown license")
        elif (license_family(lib.license) not in PERMISSIVE_FAMILIES
              and lib.name not in COPYLEFT_ALLOWLIST):
            errors.append(f"{lib.name}: {lib.license} is copyleft and not in the allowlist")
    return errors


def acknowledgements(readme: str) -> Optional[str]:
    start = readme.find("\n" + README_SECTION + "\n")
    if start < 0:
        return None
    end = readme.find("\n# ", start + len(README_SECTION) + 2)
    return readme[start:end if end >= 0 else len(readme)]


def check_readme(libs: List[Library], readme: str) -> List[str]:
    section = acknowledgements(readme)
    if section is None:
        return [f"README has no '{README_SECTION}' section"]
    errors = []
    for lib in libs:
        row = next((line for line in section.splitlines()
                    if lib.url and f"({lib.url})".lower() in line.lower()), None)
        if row is None:
            errors.append(f"{lib.name}: missing from the README acknowledgements ({lib.url})")
        elif lib.license and license_family(lib.license) not in row:
            errors.append(f"{lib.name}: README does not list its {lib.license} license")
    return errors


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("--env", default="tbeam")
    check.add_argument("--libdeps", type=Path, help="default: libdeps/<env> of the workspace")
    check.add_argument("--readme", type=Path, default=ROOT / "README.md")
    args = parser.parse_args(argv)

    libdeps = args.libdeps or workspace_dir() / "libdeps" / args.env
    if not libdeps.is_dir():
        print(f"third_party: {libdeps} does not exist; build the env first", file=sys.stderr)
        return 1
    libs = scan(libdeps)
    for lib in libs:
        print(f"{lib.name:24} {lib.version:10} {lib.license or '?':12} {lib.url}")
    errors = check_licenses(libs) + check_readme(libs, args.readme.read_text())
    for error in errors:
        print(f"third_party: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
