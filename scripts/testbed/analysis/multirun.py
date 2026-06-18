"""Aggregate metrics across many runs of a batch.

For each cell (identified by the `-<cell>-rNN` portion of the run_id), compute
mean and 95% CI for headline scalar metrics across repetitions. Rejects runs
flagged as bad (e.g., clock-skew failures or missing logs).

Headline metrics aggregated:
    routing.totals.pdr
    routing.latency_ms_overall.{p50,p95,mean}
    formation.network_convergence_at
    capacity.offered_pkt_per_min
    duty_cycle per-node mean current_mA (averaged across nodes per run)
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path

# Allow direct script invocation from any cwd:
#   python3 scripts/testbed/analysis/multirun.py <batch_dir>
if __name__ == "__main__":
    import sys
    from pathlib import Path
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.app_pdr import analyse as analyse_app_pdr
from analysis.capacity import analyse as analyse_capacity
from analysis.duty_cycle import analyse as analyse_duty
from analysis.formation import analyse as analyse_formation
from analysis.load import analyse as analyse_load
from analysis.routing import analyse as analyse_routing


def _safe(fn, run_dir):
    """Run one per-metric analysis, tolerating failure (returns None).

    Needed for the v1-vs-v2 sim comparison: v1 runs have no v2 TDMA mesh logs,
    so routing/formation/capacity/superframe analyses raise — but the run is
    still valid for the version-neutral app-layer PDR. One analysis failing must
    not reject the whole run."""
    try:
        return fn(run_dir)
    except Exception:
        return None


def _dig(d, *keys):
    """Nested .get() that returns None if any level is missing/None."""
    for k in keys:
        if not isinstance(d, dict):
            return None
        d = d.get(k)
    return d


_RUNID_CELL_RE = re.compile(
    r"^(?:[A-Za-z][\w-]*?-)?\d{8}-\d{6}-(?P<cell>.+)-r(?P<rep>\d+)$"
)

# Extract the SF integer from a cell id. Matches "SF7" and combined sweep ids
# like "SF7_d300000" (SF<n> followed by a "_d<delay_ms>" load token).
_SF_OF_CELL = re.compile(r"SF(\d+)")
# Optional load token "_d<delay_ms>" / "-d<delay_ms>" in a sweep cell id.
_DELAY_OF_CELL = re.compile(r"[_-]d(\d+)")


def _cell_of(run_dir: Path) -> tuple[str, int] | None:
    m = _RUNID_CELL_RE.match(run_dir.name)
    if not m:
        return None
    return m.group("cell"), int(m.group("rep"))


# Two-sided 95% critical values of Student's t by degrees of freedom.
# Source: standard t-tables. Beyond df=30 the difference from z=1.960 is <2%,
# so we fall back to the normal approximation.
_T_975 = {
    1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571,
    6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
    11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131,
    16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093, 20: 2.086,
    21: 2.080, 22: 2.074, 23: 2.069, 24: 2.064, 25: 2.060,
    26: 2.056, 27: 2.052, 28: 2.048, 29: 2.045, 30: 2.042,
}


def _t_critical_975(df: int) -> float:
    if df <= 0:
        raise ValueError("df must be >= 1")
    return _T_975.get(df, 1.960)


def _ci95(values: list[float]) -> tuple[float, float] | None:
    """Two-sided 95% CI of the mean using Student's t with df = n-1."""
    xs = [v for v in values if v is not None]
    if len(xs) < 2:
        return None
    mean = statistics.fmean(xs)
    sd = statistics.stdev(xs)
    half = _t_critical_975(len(xs) - 1) * sd / math.sqrt(len(xs))
    return (mean - half, mean + half)


def _agg(values: list[float | None]) -> dict:
    xs = [v for v in values if v is not None]
    if not xs:
        return {"n": 0, "mean": None, "ci95": None, "min": None, "max": None}
    ci = _ci95(xs)
    return {
        "n": len(xs),
        "mean": statistics.fmean(xs),
        "ci95": ci,
        "min": min(xs),
        "max": max(xs),
    }


def _per_run_summary(run_dir: Path) -> dict | None:
    """Run all per-metric analyses for one run; return None if logs are missing."""
    logs_dir = run_dir / "logs"
    if not logs_dir.is_dir() or not any(logs_dir.iterdir()):
        return None

    # Each analysis is guarded independently so a v2-only metric failing on a v1
    # run (no TDMA logs) doesn't discard the run's app-layer PDR.
    routing = _safe(analyse_routing, run_dir)
    formation = _safe(analyse_formation, run_dir)
    capacity = _safe(analyse_capacity, run_dir)
    duty = _safe(analyse_duty, run_dir)
    load = _safe(analyse_load, run_dir)
    app = _safe(analyse_app_pdr, run_dir)

    # Per-run mean current across nodes (for energy aggregation).
    currents = [
        v.get("mean_current_mA")
        for v in (_dig(duty, "per_node") or {}).values()
        if isinstance(v, dict) and v.get("mean_current_mA") is not None
    ]
    mean_current = (statistics.fmean(currents) if currents else None)

    return {
        "routing": routing,
        "formation": formation,
        "capacity": capacity,
        "load": load,
        "app": app,
        "duty_cycle_mean_current_mA": mean_current,
    }


def aggregate(batch_dir: Path, drop_first: int = 0) -> dict:
    per_run: dict[Path, dict] = {}
    per_cell: dict[str, list[Path]] = defaultdict(list)
    per_cell_reps: dict[str, list[tuple[int, Path]]] = defaultdict(list)
    warmup_by_cell: dict[str, list[str]] = defaultdict(list)

    for run_dir in sorted(batch_dir.iterdir()):
        if not run_dir.is_dir():
            continue
        cell_rep = _cell_of(run_dir)
        if cell_rep is None:
            continue
        cell, rep = cell_rep
        if rep == 0:
            warmup_by_cell[cell].append(run_dir.name)
            continue
        per_cell_reps[cell].append((rep, run_dir))

    dropped_by_cell: dict[str, list[str]] = {}
    for cell, rep_dirs in per_cell_reps.items():
        rep_dirs.sort(key=lambda x: x[0])
        dropped = rep_dirs[:drop_first] if drop_first > 0 else []
        kept = rep_dirs[drop_first:] if drop_first > 0 else rep_dirs
        dropped_by_cell[cell] = [d.name for _, d in dropped]
        per_cell[cell] = [d for _, d in kept]

    rejected: list[str] = []
    for cell, dirs in per_cell.items():
        for run_dir in dirs:
            summary = _per_run_summary(run_dir)
            if summary is None or "error" in summary:
                rejected.append(run_dir.name)
                continue
            per_run[run_dir] = summary

    cells: dict[str, dict] = {}
    for cell, dirs in per_cell.items():
        included = [per_run[d] for d in dirs if d in per_run]
        if not included:
            cells[cell] = {
                "n_runs": 0,
                "n_rejected": len(dirs),
                "dropped_reps": dropped_by_cell.get(cell, []),
            }
            continue

        cells[cell] = {
            "n_runs": len(included),
            "n_rejected": len(dirs) - len(included),
            "dropped_reps": dropped_by_cell.get(cell, []),
            "pdr": _agg([_dig(r, "routing", "totals", "pdr") for r in included]),
            "latency_p50_ms": _agg(
                [_dig(r, "routing", "latency_ms_overall", "p50") for r in included]),
            "latency_p95_ms": _agg(
                [_dig(r, "routing", "latency_ms_overall", "p95") for r in included]),
            "latency_mean_ms": _agg(
                [_dig(r, "routing", "latency_ms_overall", "mean") for r in included]),
            "convergence_s": _agg(
                [_dig(r, "formation", "network_convergence_at") for r in included]),
            "offered_pkt_per_min": _agg(
                [_dig(r, "capacity", "offered_pkt_per_min") for r in included]),
            "mean_current_mA": _agg(
                [r.get("duty_cycle_mean_current_mA") for r in included]),
            "rho_node": _agg(
                [_dig(r, "load", "rho_node") for r in included]),
            "rho_max": _agg(
                [_dig(r, "load", "rho_max") for r in included]),
            # Version-neutral application-layer metrics (sim v1-vs-v2 comparison):
            "app_pdr": _agg([_dig(r, "app", "totals", "pdr") for r in included]),
            "app_goodput_Bps": _agg(
                [_dig(r, "app", "totals", "goodput_Bps") for r in included]),
            "app_latency_p50_ms": _agg(
                [_dig(r, "app", "latency_ms_overall", "p50") for r in included]),
            "app_latency_p95_ms": _agg(
                [_dig(r, "app", "latency_ms_overall", "p95") for r in included]),
        }

    # Per-run scatter points: PDR/latency vs normalized load, one row per kept run.
    # The cell-mean rollup above discards the per-run granularity a load curve needs.
    per_run_points: list[dict] = []
    for cell, dirs in per_cell.items():
        m = _SF_OF_CELL.match(cell)
        sf = int(m.group(1)) if m else None
        dm = _DELAY_OF_CELL.search(cell)
        packet_delay_ms = int(dm.group(1)) if dm else None
        for d in dirs:
            r = per_run.get(d)
            if not r:
                continue
            load = r.get("load") or {}
            per_run_points.append({
                "run": d.name, "cell": cell, "sf": sf,
                "packet_delay_ms": packet_delay_ms,
                "pdr": _dig(r, "routing", "totals", "pdr"),
                "latency_p50_ms": _dig(r, "routing", "latency_ms_overall", "p50"),
                "rho_node": load.get("rho_node"),
                "rho_max": load.get("rho_max"),
                "offered_pkt_per_min_per_node": load.get("offered_pkt_per_min_per_node"),
                "bottleneck_fan_in": load.get("bottleneck_fan_in"),
                # Version-neutral app-layer metrics (the load-curve y-axes):
                "app_pdr": _dig(r, "app", "totals", "pdr"),
                "app_goodput_Bps": _dig(r, "app", "totals", "goodput_Bps"),
                "app_latency_p50_ms": _dig(r, "app", "latency_ms_overall", "p50"),
            })

    return {
        "batch_dir": str(batch_dir),
        "drop_first": drop_first,
        "cells": cells,
        "per_run_points": per_run_points,
        "rejected_runs": rejected,
        "warmup_runs": dict(warmup_by_cell),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Aggregate batch metrics across runs")
    ap.add_argument("batch_dir", type=Path,
                    help="e.g. scripts/testbed/runs/formation")
    ap.add_argument("-o", "--output", type=Path,
                    help="Path to write aggregated.json (default: <batch_dir>/aggregated.json)")
    ap.add_argument("--drop-first", type=int, default=0, metavar="N",
                    help="Drop the first N repetitions (sorted by rep number) of each cell before aggregating")
    args = ap.parse_args()
    result = aggregate(args.batch_dir, drop_first=args.drop_first)
    out = args.output or (args.batch_dir / "aggregated.json")
    out.write_text(json.dumps(result, indent=2, sort_keys=True, default=list))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
