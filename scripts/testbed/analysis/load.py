"""Offered-load normalization and per-SF sustainable-load model for one run.

The \\NLM TDMA superframe gives each node one data slot per superframe, so a node
may transmit at most once per `recurrence_s` (= superframe duration). The
*offered* application load is independent of the spreading factor, but the
*capacity* collapses as the superframe stretches with SF — so the meaningful
quantity is the normalized load

    rho = offered_rate * recurrence_s            (transmissions per slot-interval)

A relay forwards other nodes' packets through its *own* single data slot, so the
binding constraint is the busiest relay. We measure its fan-in `F` (distinct
source-flows it carries) and report:

    rho_node : mean per-node offered load / per-node slot capacity (topology-free)
    rho_max  : max over nodes of (own + forwarded) load — the bottleneck relay
    sustainable_per_source = 1 / (recurrence_s * F)   (relay reaches rho=1)
    recommended_interval_ms : send interval that holds the bottleneck at `target_rho`

Capacity inputs come from the *measured* converged superframe (superframe.extract);
if those NM log lines are absent we fall back to the analytical model (model.py).
"""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import parse_run, load_measurement_window
from analysis.superframe import extract as extract_superframe
from analysis import model as _model


def _capacity_inputs(run_dir: Path) -> dict | None:
    """Return {recurrence_s, sf, node_count, max_hops, source: measured|model}.

    Prefer the measured converged superframe; fall back to the analytical model
    when the NM superframe-config log lines are missing from this run.
    """
    sx = extract_superframe(run_dir)
    if sx and sx.get("recurrence_s"):
        return {"recurrence_s": sx["recurrence_s"], "sf": sx["sf"],
                "node_count": sx["node_count"], "max_hops": sx["max_hops"],
                "superframe_s": sx["superframe_s"], "source": "measured"}
    # Fallback: model needs sf/node_count/data_slots/duty from config.yaml.
    from analysis.superframe import _load_config
    cfg = _load_config(run_dir)
    if cfg.get("sf") is None or not cfg.get("duty_cycle"):
        return None
    # node_count and max_hops unknown without logs; use a conservative default.
    node_count = cfg.get("node_count") or 13
    m = _model.metrics(cfg["sf"], node_count=node_count,
                       data_slots=cfg.get("data_slots", 1),
                       duty_cycle=cfg["duty_cycle"], max_hops=2,
                       bw_khz=cfg.get("bw_khz", 125.0),
                       cr_denom=cfg.get("cr_denom", 7))
    return {"recurrence_s": m["recurrence_s"], "sf": cfg["sf"],
            "node_count": node_count, "max_hops": 2,
            "superframe_s": m["superframe_s"], "source": "model"}


def analyse(run_dir: Path, target_rho: float = 0.5) -> dict:
    """Compute offered-load normalization + sustainable-load model for one run.

    `target_rho` is the bottleneck-relay utilization the recommended send interval
    aims for (0.5 = 2x safety margin; lower is more conservative).
    """
    cap = _capacity_inputs(run_dir)
    if cap is None:
        return {"error": "no capacity inputs (no superframe logs and no usable config.yaml)"}
    recurrence_s = cap["recurrence_s"]

    start, end = load_measurement_window(run_dir)
    events = parse_run(run_dir)  # already windowed when lifecycle.json present

    own: Counter[str] = Counter()       # packets originated per node
    fwd: Counter[str] = Counter()       # packets forwarded per node
    relay_flows: dict[str, set[str]] = defaultdict(set)  # relay -> source-flows carried
    ts_min = ts_max = None
    for e in events:
        ts_min = e.ts if ts_min is None else min(ts_min, e.ts)
        ts_max = e.ts if ts_max is None else max(ts_max, e.ts)
        if e.kind == "data_sent":
            own[e.node] += 1
            if e.fields["via"] != e.fields["dst"]:
                relay_flows[e.fields["via"]].add(e.node)
        elif e.kind == "data_forwarded":
            fwd[e.node] += 1
            relay_flows[e.node].add(e.fields["src"])

    # Measurement-window seconds (prefer lifecycle window, else observed span).
    window_s = (end - start) if (start is not None and end is not None) else None
    if not window_s and ts_min is not None and ts_max is not None:
        window_s = max(ts_max - ts_min, 1.0)

    senders = [n for n, c in own.items() if c > 0]
    total_sent = sum(own.values())
    offered_pps_per_node = (
        (total_sent / window_s) / len(senders) if senders and window_s else 0.0)

    # Per-node slot utilization: (own + forwarded) transmissions per slot-interval.
    util = {}
    if window_s:
        for n in set(own) | set(fwd):
            util[n] = ((own[n] + fwd[n]) / window_s) * recurrence_s
    rho_node = offered_pps_per_node * recurrence_s
    rho_max = max(util.values()) if util else 0.0
    busiest_node = max(util, key=util.get) if util else None

    # Topology factor: busiest relay fan-in (+1 for the relay's own traffic).
    fan_in = max((len(s) for s in relay_flows.values()), default=0) + 1
    capacity_pps = 1.0 / recurrence_s if recurrence_s else 0.0
    sustainable_pps = capacity_pps / fan_in if fan_in else 0.0
    # Interval that holds the bottleneck relay at target_rho:
    #   bottleneck_util = per_source_pps * recurrence_s * fan_in
    recommended_interval_ms = (
        1000.0 * recurrence_s * fan_in / target_rho if target_rho > 0 else None)

    return {
        "run": run_dir.name,
        "sf": cap["sf"], "capacity_source": cap["source"],
        "recurrence_s": recurrence_s, "superframe_s": cap["superframe_s"],
        "node_count": cap["node_count"], "max_hops": cap["max_hops"],
        "window_s": window_s, "n_senders": len(senders), "total_sent": total_sent,
        "offered_pps_per_node": offered_pps_per_node,
        "offered_pkt_per_min_per_node": offered_pps_per_node * 60.0,
        "capacity_pkt_per_min": capacity_pps * 60.0,
        "rho_node": rho_node,
        "rho_max": rho_max, "busiest_node": busiest_node,
        "bottleneck_fan_in": fan_in,
        "sustainable_pkt_per_min_per_source": sustainable_pps * 60.0,
        "target_rho": target_rho,
        "recommended_interval_ms": recommended_interval_ms,
    }


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Offered-load normalization + sustainable-load model for one run")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--target-rho", type=float, default=0.5,
                    help="Bottleneck-relay utilization the recommended interval targets (default 0.5)")
    args = ap.parse_args()
    res = analyse(args.run_dir, target_rho=args.target_rho)
    out = args.run_dir / "load.json"
    out.write_text(json.dumps(res, indent=2, sort_keys=True))
    print(json.dumps(res, indent=2, sort_keys=True))
    print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
