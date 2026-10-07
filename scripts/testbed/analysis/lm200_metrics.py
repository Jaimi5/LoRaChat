"""Feature / robustness metrics for the LoRaMesher 2.0.0 (lm200) campaign.

Computes, from one run directory, the per-feature numbers the lm200 gates
check: reliable delivery (APP_ACK/APP_FAIL), group delivery (GROUP_*),
Stop/Start rejoin, NM failover, node health (heap, hangs), crashes, the LDRO
self-check, data-slot grants, and join backoff. Every section degrades to
zeros/None when its events are absent, so it is safe on any v1/v2 run.

All sections use the FULL log stream (warmup and cooldown included): these are
feature/robustness checks, and an ACK or crash in the cooldown still counts.
The measurement window is reported alongside for reference.

Usage:
    python3 scripts/testbed/analysis/lm200_metrics.py <run_dir> [--json out.json]

Importable:
    compute_all(run_dir) -> dict      # every section below
    reliable_stats(events), group_stats(events, members), stopstart_stats(...),
    nm_failover_stats(...), health_stats(...), crash_stats(...),
    data_slot_stats(events), join_stats(events), diag_counts(events)
"""

from __future__ import annotations

import json
import statistics
from collections import Counter, defaultdict
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.formation import _active_devices, nm_overlap, nm_timeline
from analysis.parse_logs import Event, load_measurement_window, parse_run

_JOIN_STATES = {3, 4}
_NM_STATE = 4
RTT_OUTLIER_MS = 10 * 60 * 1000      # an RTT above 10 min is a garbled echo_ts
HANG_GAP_S = 180.0                   # HEALTH silence before log end => suspected hang


def _pct(values: list[float], q: float) -> float | None:
    if not values:
        return None
    v = sorted(values)
    k = (len(v) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def _joined_after(events: list[Event], node: str, t: float,
                  states=_JOIN_STATES) -> tuple[float, int | None] | None:
    """First (ts, state) after t at which `node` enters a joined state.
    A "Successfully joined network" line counts as state None."""
    for ev in events:
        if ev.ts <= t or ev.node != node:
            continue
        if ev.kind == "state_change" and ev.fields["state"] in states:
            return ev.ts, ev.fields["state"]
        if ev.kind == "joined" and states == _JOIN_STATES:
            return ev.ts, None
    return None


# ── Reliable unicast ─────────────────────────────────────────────────────────

def reliable_stats(events: list[Event]) -> dict:
    """APP_TX vs APP_ACK/APP_FAIL matched by (src, seq) at the originator."""
    tx = {(e.fields["src"], e.fields["seq"]) for e in events if e.kind == "app_tx"}
    acks: dict[tuple, Event] = {}
    fails: dict[tuple, Event] = {}
    n_ack_lines = n_fail_lines = 0
    for e in events:
        if e.kind == "app_ack":
            n_ack_lines += 1
            acks.setdefault((e.fields["src"], e.fields["seq"]), e)
        elif e.kind == "app_fail":
            n_fail_lines += 1
            fails.setdefault((e.fields["src"], e.fields["seq"]), e)
    if not acks and not fails:
        return {"present": False, "n_tx": len(tx)}
    acked = set(acks) & tx if tx else set(acks)
    failed = (set(fails) - set(acks)) & tx if tx else set(fails) - set(acks)
    rtts = [float(acks[k].fields["rtt"]) for k in acks]
    sane = [r for r in rtts if r <= RTT_OUTLIER_MS]
    per_src: dict[str, dict] = defaultdict(lambda: {"tx": 0, "ack": 0, "fail": 0})
    for s, _ in tx:
        per_src[s]["tx"] += 1
    for s, _ in acked:
        per_src[s]["ack"] += 1
    for s, _ in failed:
        per_src[s]["fail"] += 1
    reasons = Counter(fails[k].fields["reason"] for k in fails)
    n_tx = len(tx)
    return {
        "present": True,
        "n_tx": n_tx,
        "n_ack": len(acked),
        "n_fail": len(failed),
        "n_unresolved": max(n_tx - len(acked) - len(failed), 0),
        "ack_ratio": (len(acked) / n_tx) if n_tx else None,
        "n_ack_lines": n_ack_lines,
        "n_fail_lines": n_fail_lines,
        "duplicate_acks": n_ack_lines - len(acks),
        "fail_reasons": dict(reasons),
        "rtt_ms_p50": _pct(sane, 0.5),
        "rtt_ms_p95": _pct(sane, 0.95),
        "rtt_ms_max": max(sane) if sane else None,
        "rtt_outliers": len(rtts) - len(sane),
        "per_src": dict(sorted(per_src.items())),
    }


# ── Group delivery ───────────────────────────────────────────────────────────

def group_stats(events: list[Event], members: set[str] | None = None) -> dict:
    """Per GROUP_TX: share of the other members that logged GROUP_RX, the
    per-member duplicate count, and the GROUP_WIN ACK distribution.

    `members` are physical node ids. Default: every node that logged any
    GROUP_RX/GROUP_TX (an under-count if a member received nothing at all,
    so callers should pass the configured membership when they know it)."""
    txs = [e for e in events if e.kind == "group_tx"]
    rxs = [e for e in events if e.kind == "group_rx"]
    wins = [e for e in events if e.kind == "group_win"]
    if not txs and not rxs:
        return {"present": False}
    if members is None:
        members = {e.node for e in rxs} | {e.node for e in txs}
    rx_by_msg: dict[tuple, Counter] = defaultdict(Counter)
    for e in rxs:
        rx_by_msg[(e.fields["src"], e.fields["grp"], e.fields["seq"])][e.node] += 1
    per_msg = []
    dup_lines = 0
    for e in txs:
        key = (e.fields["src"], e.fields["grp"], e.fields["seq"])
        others = members - {e.node}
        got = rx_by_msg.get(key, Counter())
        recv = set(got) & others
        per_msg.append({"src": key[0], "grp": key[1], "seq": key[2],
                        "received": len(recv), "members": len(others),
                        "ratio": (len(recv) / len(others)) if others else None})
    total_rx_lines = 0
    for got in rx_by_msg.values():
        for _node, n in got.items():
            total_rx_lines += n
            dup_lines += max(n - 1, 0)
    ratios = [m["ratio"] for m in per_msg if m["ratio"] is not None]
    win_acks = [e.fields["acks"] for e in wins]
    n_members_other = max(len(members) - 1, 0)
    return {
        "present": True,
        "members": sorted(members),
        "n_group_tx": len(txs),
        "delivery_ratio_mean": statistics.mean(ratios) if ratios else None,
        "delivery_ratio_min": min(ratios) if ratios else None,
        "duplicate_rx_lines": dup_lines,
        "duplicate_ratio": (dup_lines / total_rx_lines) if total_rx_lines else 0.0,
        "n_group_win": len(wins),
        "win_acks_mean": statistics.mean(win_acks) if win_acks else None,
        "win_acks_min": min(win_acks) if win_acks else None,
        "win_acks_max": max(win_acks) if win_acks else None,
        "win_ack_completeness_mean": (statistics.mean(win_acks) / n_members_other
                                      if win_acks and n_members_other else None),
        "per_msg": per_msg,
    }


# ── Stop / Start ─────────────────────────────────────────────────────────────

def stopstart_stats(events: list[Event], superframe_s: float | None = None) -> dict:
    """For each STOPSTART start: time until the node is joined again, plus
    whether its APP_TX seq kept counting up across the restart."""
    ss = [e for e in events if e.kind == "stopstart"]
    if not ss:
        return {"present": False}
    cycles = []
    for e in ss:
        if e.fields["action"] != "start":
            continue
        node = e.node
        stop_ts = max((s.ts for s in ss if s.node == node
                       and s.fields["action"] == "stop" and s.ts <= e.ts),
                      default=None)
        j = _joined_after(events, node, e.ts)
        rejoin_s = (j[0] - e.ts) if j else None
        before = [x.fields["seq"] for x in events if x.kind == "app_tx"
                  and x.node == node and stop_ts is not None and x.ts < stop_ts]
        after = [x.fields["seq"] for x in events if x.kind == "app_tx"
                 and x.node == node and x.ts > e.ts]
        seq_continues = (after[0] > before[-1]) if (before and after) else None
        cycles.append({
            "node": node, "stop_ts": stop_ts, "start_ts": e.ts,
            "start_ok": e.fields.get("ok"),
            "rejoin_s": rejoin_s,
            "rejoin_superframes": (rejoin_s / superframe_s
                                   if rejoin_s is not None and superframe_s else None),
            "seq_continues": seq_continues,
        })
    rejoins = [c["rejoin_s"] for c in cycles if c["rejoin_s"] is not None]
    sfs = [c["rejoin_superframes"] for c in cycles
           if c["rejoin_superframes"] is not None]
    return {
        "present": True,
        "n_cycles": len(cycles),
        "n_rejoined": len(rejoins),
        "n_start_failed": sum(1 for c in cycles if c["start_ok"] is False),
        "rejoin_s_max": max(rejoins) if rejoins else None,
        "rejoin_s_median": statistics.median(rejoins) if rejoins else None,
        "rejoin_superframes_max": max(sfs) if sfs else None,
        "seq_reset_cycles": sum(1 for c in cycles if c["seq_continues"] is False),
        "ctrl_slot_reuse": sum(1 for e in events if e.kind == "ctrl_slot_reuse"),
        "cycles": cycles,
    }


# ── NM failover ──────────────────────────────────────────────────────────────

def nm_failover_stats(events: list[Event], window=(None, None)) -> dict:
    """After each NM_FAILOVER: time until another node becomes NM, whether
    two managers ever overlapped, and when/how the old NM rejoined."""
    fos = [e for e in events if e.kind == "nm_failover"]
    intervals = nm_timeline(events, (None, window[1]))
    overlap = nm_overlap(intervals)
    base = {
        "max_concurrent_nm": overlap["max_concurrent_nm"],
        "dual_nm_seconds": overlap["dual_nm_seconds"],
        "election_backoff": sum(1 for e in events if e.kind == "election_backoff"),
    }
    if not fos:
        return {"present": False, **base}
    out = []
    for e in fos:
        takeover = next((x for x in events if x.ts > e.ts and x.node != e.node
                         and x.kind == "state_change"
                         and x.fields["state"] == _NM_STATE), None)
        j = _joined_after(events, e.node, e.ts)
        out.append({
            "old_nm": e.node, "ts": e.ts,
            "new_nm": takeover.node if takeover else None,
            "takeover_s": (takeover.ts - e.ts) if takeover else None,
            "old_nm_rejoin_s": (j[0] - e.ts) if j else None,
            "old_nm_rejoined_state": j[1] if j else None,
        })
    tk = [o["takeover_s"] for o in out if o["takeover_s"] is not None]
    return {
        "present": True, **base,
        "n_failovers": len(out),
        "n_taken_over": len(tk),
        "takeover_s_max": max(tk) if tk else None,
        "failovers": out,
    }


# ── Health / hangs ───────────────────────────────────────────────────────────

def health_stats(events: list[Event], log_end: float | None = None) -> dict:
    """Min free heap per node, heap trend (bytes/hour, least squares), and
    nodes whose HEALTH lines stop >HANG_GAP_S before the end of the logs."""
    hs = [e for e in events if e.kind == "health"]
    if not hs:
        return {"present": False}
    if log_end is None:
        log_end = max(e.ts for e in events)
    by_node: dict[str, list[Event]] = defaultdict(list)
    for e in hs:
        by_node[e.node].append(e)
    per_node = {}
    hung = []
    for node, lst in sorted(by_node.items()):
        xs = [(e.ts - lst[0].ts) / 3600.0 for e in lst]
        ys = [float(e.fields["heap"]) for e in lst]
        slope = None
        if len(lst) >= 3 and xs[-1] > 0:
            mx, my = statistics.mean(xs), statistics.mean(ys)
            den = sum((x - mx) ** 2 for x in xs)
            slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else None
        gap = log_end - lst[-1].ts
        if gap > HANG_GAP_S:
            hung.append(node)
        per_node[node] = {
            "n": len(lst),
            "heap_min": min(e.fields["heap"] for e in lst),
            "minheap_min": min(e.fields["minheap"] for e in lst),
            "heap_slope_bytes_per_h": slope,
            "last_health_gap_s": gap,
        }
    return {
        "present": True,
        "heap_min": min(v["heap_min"] for v in per_node.values()),
        "minheap_min": min(v["minheap_min"] for v in per_node.values()),
        "worst_heap_slope_bytes_per_h": min(
            (v["heap_slope_bytes_per_h"] for v in per_node.values()
             if v["heap_slope_bytes_per_h"] is not None), default=None),
        "suspected_hangs": hung,
        "per_node": per_node,
    }


# ── Crashes ──────────────────────────────────────────────────────────────────

def crash_stats(events: list[Event]) -> dict:
    """Unexpected reboots (an `rst:0x` not preceded by gw-reset and not the
    first boot) and panic markers, per node, over the whole run."""
    panics: dict[str, Counter] = defaultdict(Counter)
    resets: dict[str, int] = defaultdict(int)
    for e in events:
        if e.kind != "crash":
            continue
        k = e.fields["kind"]
        if k == "reset":
            if not e.fields.get("expected", True):
                resets[e.node] += 1
        else:
            panics[e.node][k] += 1
    return {
        "unexpected_resets_total": sum(resets.values()),
        "unexpected_resets": dict(sorted(resets.items())),
        "panic_markers_total": sum(sum(c.values()) for c in panics.values()),
        "panic_markers": {n: dict(c) for n, c in sorted(panics.items())},
        "nodes_affected": sorted(set(resets) | set(panics)),
    }


# ── Data slots / join / diagnostics ──────────────────────────────────────────

def data_slot_stats(events: list[Event]) -> dict:
    """Last self-reported data-slot allocation per node (DEBUG
    `Created routing table message ... data_slots: N[, ctrl_idx: I]`), plus the
    RTENTRY `slots=` view when setLogRoutingCapabilities is on."""
    own: dict[str, dict] = {}
    for e in events:
        if e.kind == "rt_msg_created":
            own[e.node] = {"data_slots": e.fields["data_slots"],
                           "ctrl_idx": e.fields["ctrl_idx"]}
    rt_slots: dict[str, int] = {}
    for e in events:
        if e.kind == "rtentry" and "slots" in e.fields and e.fields.get("active"):
            rt_slots[e.fields["dest"]] = e.fields["slots"]
    if not own and not rt_slots:
        return {"present": False}
    return {
        "present": True,
        "self_reported": dict(sorted(own.items())),
        "nodes_zero_slots": sorted(n for n, v in own.items() if v["data_slots"] == 0),
        "rtentry_slots": dict(sorted(rt_slots.items())),
    }


def join_stats(events: list[Event]) -> dict:
    """Unanswered-join retries and backoff histogram; discovery slots the
    2.0.0 join scheduler picked (should all be even)."""
    un = [e for e in events if e.kind == "join_unanswered"]
    sch = [e for e in events if e.kind == "join_scheduled"]
    per_node = Counter(e.node for e in un)
    return {
        "join_unanswered_total": len(un),
        "join_unanswered_per_node": dict(sorted(per_node.items())),
        "join_retry_max": max((e.fields["retry"] for e in un), default=0),
        "backoff_hist": {str(k): v for k, v in
                         sorted(Counter(e.fields["backoff"] for e in un).items())},
        "join_scheduled_total": len(sch),
        "join_scheduled_slot_hist": {str(k): v for k, v in
                                     sorted(Counter(e.fields["slot"] for e in sch).items())},
        "join_scheduled_odd_slots": sum(1 for e in sch if e.fields["slot"] % 2),
    }


def diag_counts(events: list[Event]) -> dict:
    """Counts of the 2.0.0 diagnostic lines (whole run)."""
    c = Counter(e.kind for e in events)
    nq = [e for e in events if e.kind == "reliable_not_queued"]
    return {
        "toa_mismatch": c.get("toa_mismatch", 0),
        "sync_discard": c.get("sync_discard", 0),
        "adverts_ignored": c.get("adverts_ignored", 0),
        "election_backoff": c.get("election_backoff", 0),
        "ctrl_slot_reuse": c.get("ctrl_slot_reuse", 0),
        "reliable_not_queued": sum(1 for e in nq if not e.fields["final"]),
        "reliable_queue_giveups": sum(1 for e in nq if e.fields["final"]),
        "linkstats_probing": sum(1 for e in events if e.kind == "linkstats"
                                 and e.fields.get("probing")),
    }


# ── Driver ───────────────────────────────────────────────────────────────────

def _group_members(run_dir: Path) -> set[str] | None:
    """Configured group membership: devices with sim_group >= 1 in config.yaml
    (defaults block applies to all). None if the key is absent."""
    import yaml
    p = run_dir / "config.yaml"
    if not p.is_file():
        return None
    cfg = yaml.safe_load(p.read_text()) or {}
    default = (cfg.get("defaults") or {}).get("sim_group")
    devs = cfg.get("devices") or {}
    found = default is not None
    members = set()
    for sid, dev in devs.items():
        dev = dev if isinstance(dev, dict) else {}
        if dev.get("node_active", 1) == 0:
            continue
        v = dev.get("sim_group", default)
        if "sim_group" in dev:
            found = True
        if v and int(v) >= 1:
            members.add(str(sid).upper())
    return members if found else None


def _superframe_s(run_dir: Path) -> float | None:
    try:
        from analysis.superframe import extract
        rec = extract(run_dir)
        return rec["superframe_s"] if rec else None
    except Exception:  # noqa: BLE001 - optional enrichment only
        return None


def compute_all(run_dir: Path, events: list[Event] | None = None) -> dict:
    run_dir = Path(run_dir)
    window = load_measurement_window(run_dir)
    if events is None:
        events = parse_run(run_dir, window=(None, None))
    log_end = max((e.ts for e in events), default=None)
    sf_s = _superframe_s(run_dir)
    return {
        "run": run_dir.name,
        "window": {"start": window[0], "end": window[1]},
        "n_events": len(events),
        "n_active_devices": len(_active_devices(run_dir)),
        "superframe_s": sf_s,
        "reliable": reliable_stats(events),
        "group": group_stats(events, _group_members(run_dir)),
        "stopstart": stopstart_stats(events, sf_s),
        "nm_failover": nm_failover_stats(events, window),
        "health": health_stats(events, log_end),
        "crashes": crash_stats(events),
        "data_slots": data_slot_stats(events),
        "join": join_stats(events),
        "diag": diag_counts(events),
    }


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="lm200 feature/robustness metrics for one run")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--json", type=Path, default=None,
                    help="write the result here (default: print to stdout)")
    args = ap.parse_args()
    res = compute_all(args.run_dir)
    text = json.dumps(res, indent=2, sort_keys=True, default=str)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(text)
        print(f"wrote {args.json}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
