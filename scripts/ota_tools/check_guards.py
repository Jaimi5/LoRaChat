#!/usr/bin/env python3
"""Static checks that keep app rollback working.

- esp_ota_mark_app_valid_cancel_rollback() is only called by the boot guard.
- verifyRollbackLater() is defined and returns true, so initArduino() leaves PENDING_VERIFY alone.
- CONFIG_ARDUINO_ISR_IRAM is not enabled and app rollback is enabled in every sdkconfig.

Exits non-zero and prints every violation.
"""

import re
import sys
from pathlib import Path
from typing import List

BOOT_GUARD = Path("src/ota/otaBootGuard.cpp")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}


def check_mark_valid(root: Path) -> List[str]:
    errors = []
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or path.relative_to(root) == BOOT_GUARD:
            continue
        for n, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if "mark_app_valid" in line:
                errors.append(f"{path.relative_to(root)}:{n}: mark_app_valid outside the boot guard")
    return errors


def check_verify_rollback_later(root: Path) -> List[str]:
    pattern = re.compile(
        r'extern\s+"C"\s+bool\s+verifyRollbackLater\s*\(\s*\)\s*\{\s*return\s+true\s*;\s*\}')
    for path in (root / "src").rglob("*.cpp"):
        if pattern.search(path.read_text(errors="replace")):
            return []
    return ['src/: no `extern "C" bool verifyRollbackLater() { return true; }`']


def check_sdkconfigs(root: Path) -> List[str]:
    errors = []
    for path in sorted(root.glob("sdkconfig.*")):
        if path.suffix in (".defaults", ".old"):
            continue
        text = path.read_text(errors="replace")
        if re.search(r"^CONFIG_ARDUINO_ISR_IRAM=y", text, re.M):
            errors.append(f"{path.name}: CONFIG_ARDUINO_ISR_IRAM is enabled")
        if not re.search(r"^CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", text, re.M):
            errors.append(f"{path.name}: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set")
        if re.search(r"^CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y", text, re.M):
            errors.append(f"{path.name}: CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE is set")
    return errors


def run(root: Path) -> List[str]:
    return check_mark_valid(root) + check_verify_rollback_later(root) + check_sdkconfigs(root)


def main(argv: List[str]) -> int:
    root = Path(argv[0]) if argv else Path(".")
    errors = run(root)
    for error in errors:
        print(error)
    if not errors:
        print("rollback guards OK")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
