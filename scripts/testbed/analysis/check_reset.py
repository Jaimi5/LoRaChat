"""Per-run sanity check: did every device actually boot at the start of the run?

A healthy run's per-device monitor log should begin with the ESP32 boot
sequence — `rst:0x...`, the IDF bootloader banner, and our firmware's first
`Build environment name:` log — because `gw-reset.sh` pulses RTS and then
holds the port open through the post-reset window, appending the boot bytes
to the same file the monitor will continue writing to.

If a log starts mid-superframe with no boot marker in its first ~100 lines,
the reset either didn't happen or the boot window was missed. Either way
the run is suspect.

Usage:
    python3 scripts/testbed/analysis/check_reset.py <run_dir>
    python3 scripts/testbed/analysis/check_reset.py <batch_root>

A run dir is one containing a `logs/` subdirectory. Anything else is treated
as a batch root and every immediate child run dir is checked.

Exit code: 0 iff every checked log has a boot marker; 1 otherwise.
"""

from __future__ import annotations

import argparse
import gzip
import sys
from pathlib import Path

# Any one of these in the first MAX_LINES of a log is enough to call it
# "booted". The ROM/bootloader markers always appear at 115200 baud; the
# firmware-side `Build environment name:` is our app's first ESP_LOGI from
# src/main.cpp:263, so it's the strongest evidence the device made it past
# bootloader and into setup(). The `--- RESET via gw-reset.sh` marker is what
# gw-reset.sh itself writes when it captures the boot window.
BOOT_MARKERS = (
    "rst:0x",
    "ets ",
    "ESP-ROM",
    "boot:",
    "app_main",
    "Build environment name:",
    "--- RESET via gw-reset.sh",
)

MAX_LINES = 100


def _open_log(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open("r", encoding="utf-8", errors="replace")


def _has_boot_marker(path: Path) -> tuple[bool, str | None]:
    """Return (found, matched_marker). Reads up to MAX_LINES lines."""
    with _open_log(path) as fh:
        for i, line in enumerate(fh):
            if i >= MAX_LINES:
                break
            for marker in BOOT_MARKERS:
                if marker in line:
                    return True, marker
    return False, None


def _short_id_from_log(path: Path) -> str:
    # monitor-dev-<session>-<SHORT_ID>.log[.gz]
    name = path.name
    if name.endswith(".gz"):
        name = name[:-3]
    if name.endswith(".log"):
        name = name[:-4]
    return name.rsplit("-", 1)[-1] if "-" in name else name


def _iter_logs(run_dir: Path):
    logs_dir = run_dir / "logs"
    if not logs_dir.is_dir():
        return []
    paths = sorted(
        list(logs_dir.glob("monitor-dev-*.log"))
        + list(logs_dir.glob("monitor-dev-*.log.gz"))
    )
    return paths


def check_run(run_dir: Path) -> tuple[int, list[str]]:
    """Return (total_devices, missing_short_ids)."""
    logs = _iter_logs(run_dir)
    missing: list[str] = []
    for log in logs:
        ok, _ = _has_boot_marker(log)
        if not ok:
            missing.append(_short_id_from_log(log))
    return len(logs), missing


def _is_run_dir(p: Path) -> bool:
    return (p / "logs").is_dir()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", type=Path, help="Run directory or batch root")
    ap.add_argument("--quiet", action="store_true",
                    help="Only print failing runs and the final summary")
    args = ap.parse_args(argv)

    root = args.path.resolve()
    if not root.exists():
        print(f"error: path not found: {root}", file=sys.stderr)
        return 2

    if _is_run_dir(root):
        run_dirs = [root]
    else:
        run_dirs = sorted(p for p in root.iterdir() if p.is_dir() and _is_run_dir(p))
        if not run_dirs:
            print(f"error: no run directories under {root}", file=sys.stderr)
            return 2

    total_runs = 0
    bad_runs = 0
    total_devices = 0
    total_missing = 0

    for run_dir in run_dirs:
        n, missing = check_run(run_dir)
        total_runs += 1
        total_devices += n
        total_missing += len(missing)
        if n == 0:
            print(f"  {run_dir.name}: no monitor logs found")
            bad_runs += 1
            continue
        if missing:
            bad_runs += 1
            print(f"  {run_dir.name}: MISSING boot banner on "
                  f"{len(missing)}/{n} device(s): {', '.join(missing)}")
        elif not args.quiet:
            print(f"  {run_dir.name}: OK ({n}/{n} devices booted)")

    print(
        f"\nchecked {total_runs} run(s), {total_devices} log(s); "
        f"{total_missing} missing boot banner, {bad_runs} run(s) with issues"
    )
    return 0 if total_missing == 0 and bad_runs == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
