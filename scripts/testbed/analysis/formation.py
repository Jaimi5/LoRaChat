"""Network formation & convergence metrics.

Per node:
    joined_at        first state_change to NORMAL_OPERATION (3) or
                     NETWORK_MANAGER (4), or first "Successfully joined network"
    rt_complete_at   first time the active routing table covers all expected peers
Network-wide:
    convergence_at   max(rt_complete_at) over all participating nodes
    churn_per_min    mean RTENTRY (dest,via) flip rate during steady state

Expected peer set defaults to all other active devices in the run's config.yaml.
"""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path
from typing import Iterable

import yaml

from .parse_logs import Event, parse_run


_JOIN_STATES = {3, 4}  # NORMAL_OPERATION, NETWORK_MANAGER


def _active_devices(run_dir: Path) -> set[str]:
    """Read config.yaml and return the set of short IDs with node_active=1."""
    cfg_path = run_dir / "config.yaml"
    if not cfg_path.is_file():
        return set()
    cfg = yaml.safe_load(cfg_path.read_text())
    out = set()
    for short_id, dev in (cfg.get("devices") or {}).items():
        if isinstance(dev, dict) and dev.get("node_active", 1) == 0:
            continue
        out.add(str(short_id).upper())
    return out


def _compute_join(events: Iterable[Event]) -> dict[str, float]:
    """First time each node enters NORMAL_OPERATION/NETWORK_MANAGER or logs
    a successful join."""
    out: dict[str, float] = {}
    for ev in events:
        if ev.node in out:
            continue
        if ev.kind == "state_change" and ev.fields.get("state") in _JOIN_STATES:
            out[ev.node] = ev.ts
        elif ev.kind == "joined":
            out[ev.node] = ev.ts
    return out


def _compute_rt_complete(events: Iterable[Event],
                         expected_peers: set[str]) -> dict[str, float]:
    """Per-node: first ts at which its active routing table contains every
    expected peer (excluding itself)."""
    seen: dict[str, set[str]] = defaultdict(set)
    out: dict[str, float] = {}
    for ev in events:
        if ev.kind != "rtentry":
            continue
        if not ev.fields.get("active"):
            continue
        node = ev.node
        if node in out:
            continue
        # `dest` in RTENTRY may be a longer-form hex; truncate to last 4 chars
        # to match short_id.
        dest = ev.fields["dest"][-4:].upper()
        if dest == node:
            continue
        seen[node].add(dest)
        # Expected target: all expected_peers except self.
        target = expected_peers - {node}
        if target and seen[node] >= target:
            out[node] = ev.ts
    return out


def _compute_churn(events: Iterable[Event], window_start: float | None,
                   window_end: float | None) -> float:
    """Mean per-node rate of via-hop flips per minute over the steady-state
    window. We count one churn event each time RTENTRY for the same dest
    reports a different `via` than the previous active entry from the same
    observing node."""
    current: dict[tuple[str, str], str] = {}
    churn: dict[str, int] = defaultdict(int)
    nodes_seen: set[str] = set()
    ts_first: float | None = None
    ts_last: float | None = None

    for ev in events:
        if ev.kind != "rtentry" or not ev.fields.get("active"):
            continue
        nodes_seen.add(ev.node)
        ts_first = ev.ts if ts_first is None else ts_first
        ts_last = ev.ts
        key = (ev.node, ev.fields["dest"])
        new_via = ev.fields["via"]
        prev = current.get(key)
        if prev is not None and prev != new_via:
            churn[ev.node] += 1
        current[key] = new_via

    if not nodes_seen or ts_first is None or ts_last is None:
        return 0.0
    duration_min = max((ts_last - ts_first) / 60.0, 1e-6)
    total_per_node = [churn[n] / duration_min for n in nodes_seen]
    return sum(total_per_node) / len(total_per_node)


def analyse(run_dir: Path) -> dict:
    events = parse_run(run_dir)
    expected = _active_devices(run_dir)

    joined = _compute_join(events)
    rt_complete = _compute_rt_complete(events, expected)

    # Express everything relative to the earliest "join" — that's a usable
    # zero for cross-run comparison. Absolute epochs are useless across runs.
    t0 = min(joined.values()) if joined else None

    def rel(ts: float | None) -> float | None:
        if ts is None or t0 is None:
            return None
        return ts - t0

    convergence_at = max(rt_complete.values()) if rt_complete else None

    return {
        "expected_peers": sorted(expected),
        "joined_at": {n: rel(ts) for n, ts in sorted(joined.items())},
        "rt_complete_at": {n: rel(ts) for n, ts in sorted(rt_complete.items())},
        "network_convergence_at": rel(convergence_at),
        "rt_churn_per_min_mean": _compute_churn(events, None, None),
        "n_active_devices": len(expected),
        "n_nodes_joined": len(joined),
        "n_nodes_rt_complete": len(rt_complete),
    }


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Compute formation metrics for one run")
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    result = analyse(args.run_dir)
    out = args.run_dir / "formation.json"
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
