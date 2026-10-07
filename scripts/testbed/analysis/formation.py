"""Network formation & convergence metrics.

Per node:
    joined_at        first state_change to NORMAL_OPERATION (3) or
                     NETWORK_MANAGER (4), or first "Successfully joined network"
    rt_complete_at   first time the active routing table covers all expected peers
Network-wide:
    convergence_at   max(rt_complete_at) over all participating nodes
    churn_per_min    mean RTENTRY (dest,via) flip rate during steady state

    join_retries_total  unanswered join attempts (2.0.0 "Join attempt
                     unanswered", 134ae25 "Join retry #") up to window end
    flaps_total      times a node left the joined state (3/4 -> other state,
                     or an unexpected reboot while joined) inside the window
    rejoins_total    re-entries into the joined state after a node's first join
    max_concurrent_nm / dual_nm_seconds
                     overlap of NETWORK_MANAGER (state 4) intervals inside the
                     window; >1 means two managers at once

The NM/flap/retry metrics are computed on the full log stream (state set during
warmup carries into the window) and then clipped to the measurement window;
the cumulative join/convergence metrics above are unchanged.

Expected peer set defaults to all other active devices in the run's config.yaml.
"""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path
from typing import Iterable

import yaml

# Allow direct script invocation from any cwd:
#   python3 scripts/testbed/analysis/formation.py <run_dir>
if __name__ == "__main__":
    import sys
    from pathlib import Path
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import (Event, is_v2_run, load_measurement_window,
                                 parse_run, v2_runtime_addr)


_JOIN_STATES = {3, 4}  # NORMAL_OPERATION, NETWORK_MANAGER
_NM_STATE = 4


def _is_unexpected_reboot(ev: Event) -> bool:
    return (ev.kind == "crash" and ev.fields.get("kind") == "reset"
            and not ev.fields.get("expected", True))


def _is_reboot(ev: Event) -> bool:
    return ev.kind == "crash" and ev.fields.get("kind") == "reset" \
        and ev.fields.get("boot_index", 0) > 0


def state_timeline(events: Iterable[Event]) -> dict[str, list[tuple[float, int]]]:
    """Per node, the ordered (ts, state) transitions from `state_change`
    events. A reboot after the first boot inserts state 0 (INITIALIZING),
    since the protocol restarts from scratch."""
    out: dict[str, list[tuple[float, int]]] = defaultdict(list)
    for ev in events:
        if ev.kind == "state_change":
            out[ev.node].append((ev.ts, ev.fields["state"]))
        elif _is_reboot(ev):
            out[ev.node].append((ev.ts, 0))
    return dict(out)


def nm_timeline(events: Iterable[Event],
                window: tuple[float | None, float | None] = (None, None),
                ) -> list[tuple[str, float, float]]:
    """NETWORK_MANAGER intervals as (node, start, end), clipped to `window`.

    Pass the *full* event stream (parse_run(run, window=(None, None))) so an
    NM elected during warmup is still seen. Open intervals end at the window
    end, or at the node's last event when no window end is given."""
    events = list(events)
    w0, w1 = window
    last_ts: dict[str, float] = {}
    for ev in events:
        last_ts[ev.node] = ev.ts
    out: list[tuple[str, float, float]] = []
    for node, trans in state_timeline(events).items():
        start: float | None = None
        for ts, st in trans:
            if st == _NM_STATE and start is None:
                start = ts
            elif st != _NM_STATE and start is not None:
                out.append((node, start, ts))
                start = None
        if start is not None:
            out.append((node, start, w1 if w1 is not None else last_ts[node]))
    clipped = []
    for node, a, b in out:
        if w0 is not None:
            a = max(a, w0)
        if w1 is not None:
            b = min(b, w1)
        if b > a:
            clipped.append((node, a, b))
    return sorted(clipped, key=lambda x: x[1])


def nm_overlap(intervals: list[tuple[str, float, float]]) -> dict:
    """Sweep the NM intervals: max simultaneous managers, total seconds with
    two or more, and the overlapping spans themselves."""
    pts = []
    for node, a, b in intervals:
        pts.append((a, 1, node))
        pts.append((b, -1, node))
    pts.sort(key=lambda p: (p[0], p[1]))  # ends before starts at equal ts
    cur: set[str] = set()
    max_c = 0
    dual_s = 0.0
    spans: list[dict] = []
    prev_ts: float | None = None
    for ts, d, node in pts:
        if prev_ts is not None and len(cur) >= 2 and ts > prev_ts:
            dual_s += ts - prev_ts
            if spans and spans[-1]["end"] == prev_ts and \
                    spans[-1]["nodes"] == sorted(cur):
                spans[-1]["end"] = ts
            else:
                spans.append({"start": prev_ts, "end": ts, "nodes": sorted(cur)})
        if d > 0:
            cur.add(node)
        else:
            cur.discard(node)
        max_c = max(max_c, len(cur))
        prev_ts = ts
    return {"max_concurrent_nm": max_c, "dual_nm_seconds": dual_s,
            "dual_nm_spans": spans}


def _compute_flaps(events: list[Event],
                   window: tuple[float | None, float | None]) -> tuple[dict, dict]:
    """Per node (flaps, rejoins) inside the window. A flap is leaving the
    joined state (3/4 -> any other state, or a reboot while joined); a rejoin
    is entering it again after the node's first join."""
    w0, w1 = window

    def inside(ts: float) -> bool:
        return (w0 is None or ts >= w0) and (w1 is None or ts <= w1)

    flaps: dict[str, int] = defaultdict(int)
    rejoins: dict[str, int] = defaultdict(int)
    for node, trans in state_timeline(events).items():
        joined = False
        ever = False
        for ts, st in trans:
            now = st in _JOIN_STATES
            if joined and not now and inside(ts):
                flaps[node] += 1
            if now and not joined:
                if ever and inside(ts):
                    rejoins[node] += 1
                ever = True
            joined = now
    return dict(flaps), dict(rejoins)


def _compute_join_retries(events: Iterable[Event],
                          window_end: float | None) -> dict[str, int]:
    out: dict[str, int] = defaultdict(int)
    for ev in events:
        if ev.kind == "join_unanswered" and (window_end is None or ev.ts <= window_end):
            out[ev.node] += 1
    return dict(out)


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


def _pick_addr_form(events: Iterable[Event], expected_peers: set[str],
                    is_v2: bool):
    """Choose the address normalisation that matches this run's RTENTRY `dest`
    values: physical (identity) or v2 runtime (bit 15 cleared).

    Returns the normalising callable. Whichever form covers more of the observed
    destinations wins; ties and empty runs fall back to physical unless the run
    has no physical evidence at all, in which case `is_v2` breaks the tie."""
    dests = {ev.fields["dest"][-4:].upper() for ev in events
             if ev.kind == "rtentry" and ev.fields.get("active")}
    if not dests:
        return v2_runtime_addr if is_v2 else (lambda a: str(a).upper())
    physical = {str(p).upper() for p in expected_peers}
    runtime = {v2_runtime_addr(p) for p in expected_peers}
    if len(dests & runtime) > len(dests & physical):
        return v2_runtime_addr
    return lambda a: str(a).upper()


def _compute_rt_complete(events: Iterable[Event],
                         expected_peers: set[str],
                         is_v2: bool = False) -> dict[str, float]:
    """Per-node: first ts at which its active routing table contains every
    expected peer (excluding itself).

    Routing-table `dest` values may be either the physical/config address or the
    *runtime* one: v2 clears bit 15 to dodge the reserved 0x8000 half (see
    `v2_runtime_addr`), but whether a given log prints the physical or the
    remapped form depends on the firmware/monitor build, not on the protocol
    version — both forms occur in v2 batches. So rather than trusting `is_v2`,
    pick the mapping that actually matches the observed `dest` values, and fall
    back to physical on a tie (the identity mapping is the safer default: it
    cannot invent matches, whereas an unnecessary remap silently makes every
    routing table look incomplete). Results stay keyed by the physical short id
    (`ev.node`) so downstream output is human-readable."""
    norm = _pick_addr_form(events, expected_peers, is_v2)
    target_all = {norm(p) for p in expected_peers}
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
        node_rt = norm(node)
        # `dest` in RTENTRY may be a longer-form hex; truncate to last 4 chars
        # to match short_id. It is a runtime address (already v2 form in v2 logs).
        dest = ev.fields["dest"][-4:].upper()
        if dest == node_rt:
            continue
        seen[node].add(dest)
        # Expected target: all expected peers except self (both in runtime form).
        target = target_all - {node_rt}
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


def _in_window(events: list[Event], window) -> list[Event]:
    start, end = window
    return [ev for ev in events
            if not (start is not None and ev.ts < start)
            and not (end is not None and ev.ts > end)]


def analyse(run_dir: Path) -> dict:
    # Parse once without a window, then filter exactly as parse_run would.
    window = load_measurement_window(run_dir)
    all_events = parse_run(run_dir, window=(None, None))
    events = _in_window(all_events, window)
    expected = _active_devices(run_dir)

    joined = _compute_join(events)
    rt_complete = _compute_rt_complete(events, expected, is_v2=is_v2_run(events))

    # Express everything relative to the earliest "join" — that's a usable
    # zero for cross-run comparison. Absolute epochs are useless across runs.
    t0 = min(joined.values()) if joined else None

    def rel(ts: float | None) -> float | None:
        if ts is None or t0 is None:
            return None
        return ts - t0

    convergence_at = max(rt_complete.values()) if rt_complete else None

    nm_int = nm_timeline(all_events, window)
    overlap = nm_overlap(nm_int)
    flaps, rejoins = _compute_flaps(all_events, window)
    retries = _compute_join_retries(all_events, window[1])

    return {
        "expected_peers": sorted(expected),
        "joined_at": {n: rel(ts) for n, ts in sorted(joined.items())},
        "rt_complete_at": {n: rel(ts) for n, ts in sorted(rt_complete.items())},
        "network_convergence_at": rel(convergence_at),
        "rt_churn_per_min_mean": _compute_churn(events, None, None),
        "n_active_devices": len(expected),
        "n_nodes_joined": len(joined),
        "n_nodes_rt_complete": len(rt_complete),
        "join_retries_total": sum(retries.values()),
        "join_retries": dict(sorted(retries.items())),
        "flaps_total": sum(flaps.values()),
        "flaps": dict(sorted(flaps.items())),
        "rejoins_total": sum(rejoins.values()),
        "rejoins": dict(sorted(rejoins.items())),
        "nm_nodes": sorted({n for n, _, _ in nm_int}),
        "nm_intervals": [{"node": n, "start": rel(a), "end": rel(b)}
                         for n, a, b in nm_int],
        "max_concurrent_nm": overlap["max_concurrent_nm"],
        "dual_nm_seconds": overlap["dual_nm_seconds"],
        "dual_nm_spans": [{"start": rel(sp["start"]), "end": rel(sp["end"]),
                           "nodes": sp["nodes"]}
                          for sp in overlap["dual_nm_spans"]],
    }


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Compute formation metrics for one run")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--out", type=Path, default=None,
                    help="output JSON path (default: <run_dir>/formation.json)")
    args = ap.parse_args()
    result = analyse(args.run_dir)
    out = args.out or (args.run_dir / "formation.json")
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
