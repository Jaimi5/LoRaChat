#!/usr/bin/env python3
"""License gate: fails when a resolved library is GPL, LGPL, AGPL or of unknown license.

    python3 scripts/ota_tools/license_gate.py .pio/libdeps/tbeam [--allow NAME ...]

The license of each library folder comes from library.json ("license"), else library.properties
("license="), else the text of its LICENSE / COPYING file. New dependencies must be permissive
(MIT, BSD, Apache-2.0); the LGPL libraries accepted on 2026-10-06 are passed with --allow.
Standard library only.
"""
import argparse
import json
import re
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional

LICENSE_FILES = re.compile(r"^(licen[cs]e|copying)(\.(txt|md))?$", re.IGNORECASE)
COPYLEFT = re.compile(r"\b(A?GPL|LGPL)", re.IGNORECASE)
# Most specific first: the Lesser and Affero texts also contain "GENERAL PUBLIC LICENSE".
TEXT_PATTERNS = [
    ("AGPL", re.compile(r"GNU AFFERO GENERAL PUBLIC LICENSE", re.IGNORECASE)),
    ("LGPL", re.compile(r"GNU LESSER GENERAL PUBLIC LICENSE|GNU LIBRARY GENERAL", re.IGNORECASE)),
    ("GPL", re.compile(r"GNU GENERAL PUBLIC LICENSE", re.IGNORECASE)),
    ("Apache-2.0", re.compile(r"Apache License,?\s+Version 2\.0", re.IGNORECASE)),
    ("MIT", re.compile(r"MIT License|Permission is hereby granted, free of charge", re.IGNORECASE)),
    ("BSD", re.compile(r"BSD License|Redistribution and use in source and binary forms",
                       re.IGNORECASE)),
]


def declared_license(path: Path) -> Optional[str]:
    manifest = path / "library.json"
    if manifest.is_file():
        try:
            value = json.loads(manifest.read_text(errors="replace")).get("license")
        except ValueError:
            value = None
        if isinstance(value, str) and value.strip():
            return value.strip()
    properties = path / "library.properties"
    if properties.is_file():
        for line in properties.read_text(errors="replace").splitlines():
            if line.lower().startswith("license=") and line.split("=", 1)[1].strip():
                return line.split("=", 1)[1].strip()
    return None


def license_from_text(path: Path) -> Optional[str]:
    for file in sorted(path.iterdir()):
        if not file.is_file() or not LICENSE_FILES.match(file.name):
            continue
        text = file.read_text(errors="replace")[:4000]
        for name, pattern in TEXT_PATTERNS:
            if pattern.search(text):
                return name
    return None


def find_licenses(libdeps: Path) -> Dict[str, str]:
    licenses = {}
    for path in sorted(Path(libdeps).iterdir()):
        if path.is_dir() and not path.name.startswith("."):
            licenses[path.name] = declared_license(path) or license_from_text(path) or "unknown"
    return licenses


def check(licenses: Dict[str, str], allow: Iterable[str] = ()) -> List[str]:
    """Returns the libraries that fail the gate."""
    allowed = set(allow)
    return [name for name, value in licenses.items()
            if name not in allowed and (value == "unknown" or COPYLEFT.search(value))]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("libdeps", type=Path, help="PlatformIO libdeps folder of one env")
    parser.add_argument("--allow", nargs="*", default=[], help="library folders accepted anyway")
    args = parser.parse_args(argv)

    licenses = find_licenses(args.libdeps)
    for name, value in licenses.items():
        print(f"{name}: {value}")
    problems = check(licenses, args.allow)
    for name in problems:
        print(f"Error: {name} is {licenses[name]}; only permissive licenses are allowed",
              file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
