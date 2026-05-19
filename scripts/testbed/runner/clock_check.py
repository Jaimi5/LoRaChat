"""Measure inter-gateway clock skew via `deploy.sh sync-time`.

Each gateway writes its monitor log timestamps using its own local clock.
If gateways drift relative to the runner host, the timestamps from different
gateways are not directly comparable. We parse `sync-time`'s per-gateway
offset output here; the runner uses two samples (before warmup and after
cooldown) to linearly interpolate a correction in `log_corrector`.

Sign convention (matches deploy.sh:781): offset_ms = remote_epoch - host_epoch.
To convert a gateway-local timestamp back to host (UTC) time, subtract the
offset.
"""

from __future__ import annotations

import json
import re
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


# Machine-parseable lines emitted by `deploy.sh sync-time` look like:
#   JSON_OFFSET: {"gw":"GW-1","offset_ms":-42,"host_epoch_ms":1747...,"reachable":true}
#   JSON_OFFSET: {"gw":"GW-7","reachable":false}
_JSON_OFFSET_RE = re.compile(r"^JSON_OFFSET:\s*(\{.*\})\s*$", re.MULTILINE)

# Legacy fallback: parse the colored human line if JSON_OFFSET is absent
# (e.g. when running against an older deploy.sh). ANSI escapes are stripped
# first via _ANSI_RE.
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
# Old deploy.sh used two separate human-readable formats:
#   in-spec:  "GW-1  OK (offset: -42ms)"
#   skewed:   "GW-2  OFFSET: 1500ms — attempting sync..."
_LEGACY_OFFSET_RE = re.compile(
    r"\b(GW-\d+)\b.*?(?:\(offset:|OFFSET:)\s*(-?\d+)ms"
)


@dataclass(frozen=True)
class OffsetSample:
    gw_id: str
    offset_ms: int          # remote_epoch - host_epoch; sign matches deploy.sh
    host_epoch_ms: int      # host epoch at the moment SSH was issued
    reachable: bool


@dataclass(frozen=True)
class SkewReport:
    samples: tuple[OffsetSample, ...]
    measured_at_ms: int     # host epoch at start of the measurement call
    raw_output: str

    @property
    def reachable_samples(self) -> tuple[OffsetSample, ...]:
        return tuple(s for s in self.samples if s.reachable)

    @property
    def max_abs_offset_ms(self) -> int:
        offs = [abs(s.offset_ms) for s in self.reachable_samples]
        return max(offs) if offs else 0

    def ok(self, max_offset_ms: int) -> bool:
        return self.max_abs_offset_ms <= max_offset_ms

    def by_gw(self) -> dict[str, OffsetSample]:
        return {s.gw_id: s for s in self.samples}


def measure_offsets(deploy_sh: Path, repo_root: Path) -> SkewReport:
    """Invoke `deploy.sh sync-time` once and parse per-gateway offsets.

    Returns a SkewReport with one OffsetSample per gateway encountered in
    the output. Unreachable gateways appear with reachable=False.
    """
    started_ms = int(time.time() * 1000)
    proc = subprocess.run(
        ["bash", str(deploy_sh), "sync-time"],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )
    output = proc.stdout + proc.stderr
    samples = _parse_samples(output, fallback_host_epoch_ms=started_ms)
    return SkewReport(
        samples=tuple(samples),
        measured_at_ms=started_ms,
        raw_output=output,
    )


def _parse_samples(output: str, fallback_host_epoch_ms: int) -> list[OffsetSample]:
    """Extract OffsetSamples from sync-time output.

    Prefers structured JSON_OFFSET lines; falls back to ANSI-stripped legacy
    parsing if no JSON lines are present (older deploy.sh).
    """
    samples: dict[str, OffsetSample] = {}

    for m in _JSON_OFFSET_RE.finditer(output):
        try:
            obj = json.loads(m.group(1))
        except json.JSONDecodeError:
            continue
        gw_id = obj.get("gw")
        if not gw_id:
            continue
        if not obj.get("reachable", False):
            samples[gw_id] = OffsetSample(
                gw_id=gw_id,
                offset_ms=0,
                host_epoch_ms=fallback_host_epoch_ms,
                reachable=False,
            )
        else:
            samples[gw_id] = OffsetSample(
                gw_id=gw_id,
                offset_ms=int(obj["offset_ms"]),
                host_epoch_ms=int(obj.get("host_epoch_ms", fallback_host_epoch_ms)),
                reachable=True,
            )

    if samples:
        return list(samples.values())

    # Legacy fallback: no JSON_OFFSET lines found — parse the human-readable
    # colorized output. We can't recover host_epoch_ms precisely; use the
    # caller's overall start-of-call timestamp.
    stripped = _ANSI_RE.sub("", output)
    for m in _LEGACY_OFFSET_RE.finditer(stripped):
        gw_id, offset_ms = m.group(1), int(m.group(2))
        samples[gw_id] = OffsetSample(
            gw_id=gw_id,
            offset_ms=offset_ms,
            host_epoch_ms=fallback_host_epoch_ms,
            reachable=True,
        )
    return list(samples.values())


def list_device_map(deploy_sh: Path, repo_root: Path) -> dict[str, str]:
    """Return SHORT_ID → GW_ID mapping by invoking `deploy.sh list-device-map`.

    The subcommand emits `{"GW-1":["E464",...], "GW-2":[...], ...}`; we
    flatten it into a per-device lookup. Raises RuntimeError if the call
    fails or output cannot be parsed.
    """
    proc = subprocess.run(
        ["bash", str(deploy_sh), "list-device-map"],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"deploy.sh list-device-map failed (rc={proc.returncode}): {proc.stderr.strip()}"
        )
    try:
        raw = json.loads(proc.stdout.strip())
    except json.JSONDecodeError as e:
        raise RuntimeError(f"could not parse list-device-map JSON: {e}\n{proc.stdout!r}") from e
    out: dict[str, str] = {}
    for gw_id, short_ids in raw.items():
        for short in short_ids:
            out[short] = gw_id
    return out


# ── Back-compat shim ─────────────────────────────────────────────────────────
# Kept so any other caller of check_skew() keeps working. Not used by the new
# runner path.

@dataclass(frozen=True)
class _LegacySkewReport:
    max_abs_offset_ms: int
    raw_output: str
    ok: bool


def check_skew(deploy_sh: Path, repo_root: Path, max_offset_ms: int = 50) -> _LegacySkewReport:
    report = measure_offsets(deploy_sh, repo_root)
    return _LegacySkewReport(
        max_abs_offset_ms=report.max_abs_offset_ms,
        raw_output=report.raw_output,
        ok=report.ok(max_offset_ms),
    )
