"""Rewrite monitor-log timestamps to compensate for per-gateway clock skew.

Each gateway runs `gw-monitor.sh`, which prefixes every captured serial line
with `[YYYY-MM-DD HH:MM:SS.mmm]` using the gateway's own system clock. If a
gateway's clock drifts vs. the runner host (treated as truth), those
timestamps are off by the same amount.

`clock_check.measure_offsets` samples each gateway's offset once before the
run (t0) and once after (t1). For each log line, we linearly interpolate
the offset between t0 and t1 and subtract it from the line's timestamp.
When t1 is missing (gateway became unreachable, etc.) we fall back to a
flat shift using only t0.

The corrected file is written under the original filename; the unmodified
original is preserved alongside it as `<name>.raw[.gz]`. If the `.raw`
backup already exists (a re-run on an already-corrected directory) the file
is left untouched.
"""

from __future__ import annotations

import gzip
import re
import shutil
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

from runner.clock_check import OffsetSample, SkewReport


# Same shape that analysis/parse_logs.py:14 expects to see post-correction.
_TS_RE = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\](.*)$")
_TS_FMT = "%Y-%m-%d %H:%M:%S.%f"

# monitor-dev-<session>-<SHORT_ID>.log[.gz] — SHORT_ID is the 4 hex chars
# after the last hyphen of DEVICE_ID (see gw-monitor.sh:46,48).
_FILE_RE = re.compile(r"^monitor-dev-.+-([0-9A-Fa-f]{4})\.log(?:\.gz)?$")


@dataclass
class FileCorrection:
    file: str
    gw_id: str | None
    mode: str               # "interpolated" | "flat" | "skipped" | "noop"
    reason: str | None = None
    lines_total: int = 0
    lines_corrected: int = 0
    min_shift_ms: float | None = None
    max_shift_ms: float | None = None

    def to_dict(self) -> dict:
        return {
            "file": self.file,
            "gw_id": self.gw_id,
            "mode": self.mode,
            "reason": self.reason,
            "lines_total": self.lines_total,
            "lines_corrected": self.lines_corrected,
            "min_shift_ms": self.min_shift_ms,
            "max_shift_ms": self.max_shift_ms,
        }


@dataclass
class CorrectionSummary:
    results: list[FileCorrection] = field(default_factory=list)

    @property
    def corrected_count(self) -> int:
        return sum(1 for r in self.results if r.mode in ("interpolated", "flat"))

    @property
    def skipped_count(self) -> int:
        return sum(1 for r in self.results if r.mode == "skipped")

    def to_dict(self) -> dict:
        return {
            "corrected": self.corrected_count,
            "skipped": self.skipped_count,
            "files": [r.to_dict() for r in self.results],
        }


def correct_logs(
    logs_dir: Path,
    report_t0: SkewReport,
    report_t1: SkewReport | None,
    short_id_to_gw: dict[str, str],
) -> CorrectionSummary:
    """Rewrite every monitor-dev-*.log[.gz] in `logs_dir` using per-gateway offsets.

    Files that don't match the expected pattern, or whose SHORT_ID isn't in
    the device map, or whose gateway has no t0 sample, are skipped (recorded
    in the summary with `mode == "skipped"` and a reason).
    """
    summary = CorrectionSummary()
    if not logs_dir.is_dir():
        return summary

    t0_by_gw = report_t0.by_gw()
    t1_by_gw = report_t1.by_gw() if report_t1 is not None else {}

    for path in sorted(logs_dir.iterdir()):
        if not path.is_file():
            continue
        m = _FILE_RE.match(path.name)
        if not m:
            continue

        short_id = m.group(1)
        gw_id = (
            short_id_to_gw.get(short_id)
            or short_id_to_gw.get(short_id.upper())
            or short_id_to_gw.get(short_id.lower())
        )

        result = FileCorrection(file=path.name, gw_id=gw_id, mode="skipped")
        summary.results.append(result)

        if gw_id is None:
            result.reason = f"unknown SHORT_ID '{short_id}' not in device map"
            continue

        s0 = t0_by_gw.get(gw_id)
        if s0 is None or not s0.reachable:
            result.reason = f"no reachable t0 sample for {gw_id}"
            continue

        s1 = t1_by_gw.get(gw_id)
        if s1 is None or not s1.reachable or s1.host_epoch_ms <= s0.host_epoch_ms:
            interp_sample = None
            mode = "flat"
        else:
            interp_sample = s1
            mode = "interpolated"

        backup = _backup_path(path)
        if backup.exists():
            result.mode = "noop"
            result.reason = "already corrected (.raw backup present)"
            continue

        try:
            stats = _rewrite_file(path, backup, s0, interp_sample)
        except OSError as e:
            # Restore from backup if we managed to move it but failed mid-write.
            if backup.exists() and not path.exists():
                shutil.move(str(backup), str(path))
            result.reason = f"I/O error: {e}"
            continue

        result.mode = mode
        result.lines_total = stats[0]
        result.lines_corrected = stats[1]
        result.min_shift_ms = stats[2]
        result.max_shift_ms = stats[3]

    return summary


def _backup_path(path: Path) -> Path:
    """`foo.log` → `foo.log.raw`; `foo.log.gz` → `foo.log.raw.gz`."""
    if path.name.endswith(".log.gz"):
        return path.with_name(path.name[: -len(".log.gz")] + ".log.raw.gz")
    return path.with_name(path.name + ".raw")


def _rewrite_file(
    target: Path,
    backup: Path,
    s0: OffsetSample,
    s1: OffsetSample | None,
) -> tuple[int, int, float | None, float | None]:
    """Move `target` to `backup`, then stream-correct from backup back to target.

    Returns (lines_total, lines_corrected, min_shift_ms, max_shift_ms).
    """
    is_gz = target.suffix == ".gz"
    shutil.move(str(target), str(backup))

    opener = gzip.open if is_gz else open

    lines_total = 0
    lines_corrected = 0
    min_shift: float | None = None
    max_shift: float | None = None

    with opener(backup, "rt", encoding="utf-8", errors="replace") as fin, \
         opener(target, "wt", encoding="utf-8") as fout:
        for line in fin:
            lines_total += 1
            line_no_nl = line.rstrip("\n")
            had_nl = len(line) > len(line_no_nl)
            m = _TS_RE.match(line_no_nl)
            if not m:
                fout.write(line)
                continue

            ts_str, rest = m.group(1), m.group(2)
            try:
                dt = datetime.strptime(ts_str, _TS_FMT).replace(tzinfo=timezone.utc)
            except ValueError:
                fout.write(line)
                continue

            line_ms = dt.timestamp() * 1000.0

            if s1 is None:
                shift_ms = float(s0.offset_ms)
            else:
                denom = float(s1.host_epoch_ms - s0.host_epoch_ms)
                alpha = (line_ms - s0.host_epoch_ms) / denom
                alpha = max(0.0, min(1.0, alpha))
                shift_ms = s0.offset_ms + alpha * (s1.offset_ms - s0.offset_ms)

            corrected_ms = line_ms - shift_ms
            corrected_dt = datetime.fromtimestamp(corrected_ms / 1000.0, tz=timezone.utc)
            corrected_str = (
                corrected_dt.strftime("%Y-%m-%d %H:%M:%S.")
                + f"{corrected_dt.microsecond // 1000:03d}"
            )

            out_line = f"[{corrected_str}]{rest}"
            if had_nl:
                out_line += "\n"
            fout.write(out_line)

            lines_corrected += 1
            if min_shift is None or shift_ms < min_shift:
                min_shift = shift_ms
            if max_shift is None or shift_ms > max_shift:
                max_shift = shift_ms

    return (lines_total, lines_corrected, min_shift, max_shift)
