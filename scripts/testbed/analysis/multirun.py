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

from .capacity import analyse as analyse_capacity
from .duty_cycle import analyse as analyse_duty
from .formation import analyse as analyse_formation
from .routing import analyse as analyse_routing


_RUNID_CELL_RE = re.compile(r"^\d{8}-\d{6}-(?P<cell>.+)-r(?P<rep>\d+)$")


def _cell_of(run_dir: Path) -> tuple[str, int] | None:
    m = _RUNID_CELL_RE.match(run_dir.name)
    if not m:
        return None
    return m.group("cell"), int(m.group("rep"))


def _ci95(values: list[float]) -> tuple[float, float] | None:
    """Half-width 95% CI of the mean using Student's t approximation (z=1.96)."""
    xs = [v for v in values if v is not None]
    if len(xs) < 2:
        return None
    mean = statistics.fmean(xs)
    sd = statistics.stdev(xs)
    half = 1.96 * sd / math.sqrt(len(xs))
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

    try:
        routing = analyse_routing(run_dir)
        formation = analyse_formation(run_dir)
        capacity = analyse_capacity(run_dir)
        duty = analyse_duty(run_dir)
    except Exception as e:
        return {"error": repr(e)}

    # Per-run mean current across nodes (for energy aggregation).
    currents = [
        v.get("mean_current_mA")
        for v in (duty.get("per_node") or {}).values()
        if isinstance(v, dict) and v.get("mean_current_mA") is not None
    ]
    mean_current = (statistics.fmean(currents) if currents else None)

    return {
        "routing": routing,
        "formation": formation,
        "capacity": capacity,
        "duty_cycle_mean_current_mA": mean_current,
    }


def aggregate(batch_dir: Path) -> dict:
    per_run: dict[Path, dict] = {}
    per_cell: dict[str, list[Path]] = defaultdict(list)

    for run_dir in sorted(batch_dir.iterdir()):
        if not run_dir.is_dir():
            continue
        cell_rep = _cell_of(run_dir)
        if cell_rep is None:
            continue
        cell, _rep = cell_rep
        per_cell[cell].append(run_dir)

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
            cells[cell] = {"n_runs": 0, "n_rejected": len(dirs)}
            continue

        cells[cell] = {
            "n_runs": len(included),
            "n_rejected": len(dirs) - len(included),
            "pdr": _agg([r["routing"]["totals"]["pdr"] for r in included]),
            "latency_p50_ms": _agg(
                [r["routing"]["latency_ms_overall"]["p50"] for r in included]),
            "latency_p95_ms": _agg(
                [r["routing"]["latency_ms_overall"]["p95"] for r in included]),
            "latency_mean_ms": _agg(
                [r["routing"]["latency_ms_overall"]["mean"] for r in included]),
            "convergence_s": _agg(
                [r["formation"]["network_convergence_at"] for r in included]),
            "offered_pkt_per_min": _agg(
                [r["capacity"]["offered_pkt_per_min"] for r in included]),
            "mean_current_mA": _agg(
                [r["duty_cycle_mean_current_mA"] for r in included]),
        }

    return {
        "batch_dir": str(batch_dir),
        "cells": cells,
        "rejected_runs": rejected,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Aggregate batch metrics across runs")
    ap.add_argument("batch_dir", type=Path,
                    help="e.g. scripts/testbed/runs/formation")
    ap.add_argument("-o", "--output", type=Path,
                    help="Path to write aggregated.json (default: <batch_dir>/aggregated.json)")
    args = ap.parse_args()
    result = aggregate(args.batch_dir)
    out = args.output or (args.batch_dir / "aggregated.json")
    out.write_text(json.dumps(result, indent=2, sort_keys=True, default=list))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
