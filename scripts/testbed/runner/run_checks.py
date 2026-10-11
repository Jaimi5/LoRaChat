#!/usr/bin/env python3
"""Post-run validity checks: host suspend, stretched windows, log gaps, config leaks.

Used by lm200/campaign.py (a failing check makes the run invalid; it is moved
aside and the cell re-run) and runnable on historical runs to audit them:

    python3 scripts/testbed/runner/run_checks.py runs/<batch> [runs/<batch>/<run> ...]

Checks (each returns ok=True/False, or None when it cannot apply):

  host_suspend  lifecycle.json clock-run-start/-end marks: (CLOCK_BOOTTIME -
                monotonic) or (wall - monotonic) grew by > SUSPEND_MAX_S. The
                runner's waits run on the monotonic clock, which stops while
                the host or the WSL VM sleeps.
  window        warmup-start -> cooldown-end took longer than
                WINDOW_FACTOR x (warmup + measurement + cooldown) + WINDOW_SLACK_S.
  log_gap       no board logged anything for > LOG_GAP_MAX_S at the same time
                (every serial monitor stalled together).
  config_leak   every active board's `CFG node=...` line (once per Start) must
                match config.yaml for that board (defaults from the committed
                src/config.h where config.yaml leaves a key out). Runs whose logs
                have no CFG line at all (firmware before CFG) are skipped.
"""

from __future__ import annotations

import gzip
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

TESTBED = Path(__file__).resolve().parents[1]
REPO = TESTBED.parents[1]
sys.path.insert(0, str(TESTBED / "configtool"))

import yaml  # noqa: E402

from schema import param as _param  # noqa: E402

SUSPEND_MAX_S = 60.0
WINDOW_FACTOR = 1.25
WINDOW_SLACK_S = 600.0
LOG_GAP_MAX_S = 300.0

# CFG field -> (config.yaml key, compare as). The firmware prints the values it
# was compiled with, so a difference means src/config.h was not what the
# experiment asked for.
CFG_FIELDS = {
    "sf": ("lora_spreading_factor", "int"),
    "pdr": ("sim_pdr_compare", "int"),
    "sleep": ("sim_sleep_mode", "int"),
    "msf": ("lora_min_sleep_fraction", "float"),
    "dds": ("lora_default_data_slots", "int"),
    "mgr": ("lora_manager_id", "hex"),
    "pc": ("packet_count", "int"),
    "pd": ("packet_delay", "int"),
    "ss": ("sim_stopstart", "int"),
    "rel": ("sim_reliable", "int"),
    "grp": ("sim_group", "int"),
    "send": ("sim_send_path", "int"),   # absent from CFG lines of older firmware: not compared
}
# Used only when the committed config.h does not define the key (older trees).
FALLBACK_DEFAULTS = {"sim_sleep_mode": 0, "lora_min_sleep_fraction": 0.0, "sim_send_path": 0}
MSF_TOL = 0.005

_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_CFG = re.compile(r"\bCFG (node=0x[0-9A-Fa-f]+ .*)$")
_KV = re.compile(r"(\w+)=(\S+)")
_LOG = re.compile(r"monitor-dev-.+?-([0-9A-Fa-f]{4})\.log(?:\.gz)?$")


def _open(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open("r", encoding="utf-8", errors="replace")


def _logs(run_dir: Path) -> list[tuple[str, Path]]:
    d = run_dir / "logs"
    if not d.is_dir():
        return []
    out = []
    for p in sorted(d.glob("monitor-dev-*.log*")):
        m = _LOG.search(p.name)
        if m:
            out.append((m.group(1).upper(), p))
    return out


def load_lifecycle(run_dir: Path) -> list[dict]:
    try:
        return json.loads((run_dir / "lifecycle.json").read_text())
    except (OSError, json.JSONDecodeError):
        return []


def _epoch(iso: str | None) -> float | None:
    return datetime.fromisoformat(iso).timestamp() if iso else None


# ── host suspend / stretched window ───────────────────────────────────────────


def suspend_check(phases: list[dict], max_s: float = SUSPEND_MAX_S) -> tuple[bool | None, str]:
    marks = {p["name"]: p for p in phases if p.get("name") in ("clock-run-start", "clock-run-end")}
    a, b = marks.get("clock-run-start"), marks.get("clock-run-end")
    if not a or not b or a.get("monotonic") is None or b.get("monotonic") is None:
        return None, "no clock-run-start/-end marks (runner predates them)"
    mono = b["monotonic"] - a["monotonic"]
    grew = {}
    for clk in ("boottime", "wall"):
        if a.get(clk) is not None and b.get(clk) is not None:
            grew[clk] = (b[clk] - a[clk]) - mono
    worst = max(grew.values(), default=0.0)
    det = ", ".join(f"{k}-monotonic grew {v:.0f} s" for k, v in grew.items())
    if worst > max_s:
        return False, f"host suspended during the run ({det}; limit {max_s:.0f} s)"
    return True, det or "no comparable clocks"


def window_check(phases: list[dict], factor: float = WINDOW_FACTOR,
                 slack_s: float = WINDOW_SLACK_S) -> tuple[bool | None, str]:
    by = {p.get("name"): p for p in phases}
    w, m, c = by.get("warmup"), by.get("measurement"), by.get("cooldown")
    if not (w and m and c and w.get("start") and c.get("end")):
        return None, "no warmup/measurement/cooldown phases (warmup rep or aborted run)"
    planned = sum(float(p.get("seconds") or 0) for p in (w, m, c))
    span = _epoch(c["end"]) - _epoch(w["start"])
    limit = factor * planned + slack_s
    det = f"warmup->cooldown took {span / 60:.1f} min, planned {planned / 60:.1f} min (limit {limit / 60:.1f})"
    return (span <= limit), det


# ── simultaneous log gap ─────────────────────────────────────────────────────


def _line_seconds(line: str, cache: dict) -> int | None:
    # "[YYYY-MM-DD HH:MM:SS.mmm] ..." -> whole seconds (fast path, minute cache)
    if len(line) < 21 or line[0] != "[" or line[11] != " ":
        return None
    minute = line[1:17]
    base = cache.get(minute)
    if base is None:
        try:
            base = int(datetime.strptime(minute, "%Y-%m-%d %H:%M")
                       .replace(tzinfo=timezone.utc).timestamp())
        except ValueError:
            return None
        cache[minute] = base
    try:
        return base + int(line[18:20])
    except ValueError:
        return None


def log_gap_check(run_dir: Path, max_gap_s: float = LOG_GAP_MAX_S) -> tuple[bool | None, str]:
    logs = _logs(run_dir)
    if not logs:
        return None, "no monitor logs"
    seconds: set[int] = set()
    cache: dict = {}
    for _node, p in logs:
        try:
            with _open(p) as fh:
                for line in fh:
                    t = _line_seconds(line, cache)
                    if t is not None:
                        seconds.add(t)
        except (OSError, EOFError):
            continue
    if len(seconds) < 2:
        return None, "no timestamped log lines"
    ts = sorted(seconds)
    gap, at = max((b - a, a) for a, b in zip(ts, ts[1:]))
    when = datetime.fromtimestamp(at, timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    det = f"longest silence of all boards together {gap} s (from {when} gateway-local)"
    return (gap <= max_gap_s), det


# ── config leak (CFG line vs config.yaml) ────────────────────────────────────


def committed_config_h(repo: Path = REPO) -> str:
    """src/config.h as committed at HEAD (what the gateways reset to), falling
    back to the working-tree file."""
    r = subprocess.run(["git", "show", "HEAD:src/config.h"], cwd=repo,
                       capture_output=True, text=True)
    if r.returncode == 0 and r.stdout:
        return r.stdout
    p = repo / "src" / "config.h"
    return p.read_text(errors="replace") if p.is_file() else ""


def config_h_defaults(text: str) -> dict:
    """First `#define` of every CFG-checked key in config.h, parsed by its schema kind."""
    out = dict(FALLBACK_DEFAULTS)
    for key, _ in CFG_FIELDS.values():
        p = _param(key)
        m = re.search(rf"^\s*#define\s+{p.define}\s+([^\s/]+)", text, re.M)
        if m:
            try:
                out[key] = p.parse_c(m.group(1))
            except ValueError:
                pass
    return out


def _norm(value, how: str):
    if value is None:
        return None
    if how == "hex":
        return value if isinstance(value, int) else int(str(value), 16)
    if how == "float":
        return float(value)
    return int(str(value), 0) if isinstance(value, str) else int(value)


def expected_per_device(run_dir: Path, defaults_h: dict) -> dict[str, dict]:
    """Effective CFG-checked values per ACTIVE device: config.h <- defaults <- device."""
    raw = yaml.safe_load((run_dir / "config.yaml").read_text()) or {}
    dflt = raw.get("defaults") or {}
    out = {}
    for sid, dev in (raw.get("devices") or {}).items():
        dev = dev if isinstance(dev, dict) else {}
        merged = {**dflt, **dev}
        if str(merged.get("node_active", 1)).lower() in ("0", "false"):
            continue
        out[str(sid).upper()] = {k: merged.get(k, defaults_h.get(k)) for k, _ in CFG_FIELDS.values()}
    return out


def read_cfg_lines(path: Path) -> list[dict]:
    out = []
    with _open(path) as fh:
        for line in fh:
            if "CFG node=" not in line:
                continue
            m = _CFG.search(_ANSI.sub("", line).rstrip())
            if m:
                out.append(dict(_KV.findall(m.group(1))))
    return out


def config_leak_check(run_dir: Path, config_h_text: str | None = None) -> tuple[bool | None, str]:
    logs = _logs(run_dir)
    found = {node: read_cfg_lines(p) for node, p in logs}
    if not any(found.values()):
        return None, "skipped: no CFG line in any log (firmware predates CFG)"
    if not (run_dir / "config.yaml").is_file():
        return None, "skipped: no config.yaml"
    defaults_h = config_h_defaults(config_h_text if config_h_text is not None else committed_config_h())
    want = expected_per_device(run_dir, defaults_h)
    bad, notes = [], []
    for node, exp in sorted(want.items()):
        lines = found.get(node) or []
        if not lines:
            notes.append(node)
            continue
        for i, got in enumerate(lines):
            diffs = []
            for field, (key, how) in CFG_FIELDS.items():
                if field not in got or exp.get(key) is None:
                    continue    # a field the firmware does not print yet is absent, not a mismatch
                try:
                    g, e = _norm(got[field], how), _norm(exp[key], how)
                except (TypeError, ValueError):
                    diffs.append(f"{field}={got[field]}?")
                    continue
                if (abs(g - e) > MSF_TOL) if how == "float" else (g != e):
                    diffs.append(f"{field}={got[field]} (want {exp[key]})")
            if diffs:
                bad.append(f"{node}#{i + 1}: " + ", ".join(diffs))
    note = f"; no CFG line on {notes}" if notes else ""
    if bad:
        return False, "config leak: " + "; ".join(bad)[:900] + note
    n = sum(len(v) for v in found.values())
    return True, f"{n} CFG line(s) match config.yaml on {len(want) - len(notes)} board(s){note}"


# ── driver ───────────────────────────────────────────────────────────────────


def check_run(run_dir: Path, config_h_text: str | None = None,
              which=("host_suspend", "window", "log_gap", "config_leak")) -> list[dict]:
    phases = load_lifecycle(run_dir)
    out = []
    for name in which:
        if name == "host_suspend":
            ok, det = suspend_check(phases)
        elif name == "window":
            ok, det = window_check(phases)
        elif name == "log_gap":
            ok, det = log_gap_check(run_dir)
        else:
            ok, det = config_leak_check(run_dir, config_h_text)
        out.append({"check": name, "ok": ok, "detail": det})
    return out


def invalid_reason(results: list[dict]) -> str | None:
    bad = [f"{r['check']}: {r['detail']}" for r in results if r["ok"] is False]
    return "; ".join(bad) if bad else None


def _run_dirs(path: Path) -> list[Path]:
    if (path / "logs").is_dir() or (path / "lifecycle.json").is_file():
        return [path]
    return sorted(d for d in path.iterdir() if d.is_dir() and (d / "lifecycle.json").is_file()) \
        if path.is_dir() else []


def main(argv=None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", type=Path, help="run dirs or batch dirs")
    ap.add_argument("--json", type=Path, default=None)
    ap.add_argument("--only", nargs="+", default=None,
                    choices=["host_suspend", "window", "log_gap", "config_leak"])
    a = ap.parse_args(argv)
    cfg_h = committed_config_h()
    rows, n_bad = [], 0
    for p in a.paths:
        for r in _run_dirs(p):
            res = check_run(r, cfg_h, which=tuple(a.only) if a.only else
                            ("host_suspend", "window", "log_gap", "config_leak"))
            why = invalid_reason(res)
            n_bad += bool(why)
            rows.append({"run": str(r), "invalid": why, "checks": res})
            print(f"{'INVALID' if why else 'ok     '}  {r.name}")
            for c in res:
                if c["ok"] is False:
                    print(f"           {c['check']}: {c['detail']}")
    print(f"\n{len(rows)} run(s), {n_bad} invalid")
    if a.json:
        a.json.write_text(json.dumps(rows, indent=2))
    return 1 if n_bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
