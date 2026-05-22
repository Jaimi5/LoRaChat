"""Routing performance: PDR, end-to-end latency, hop distribution, route stability.

Match key for a flow: (src, dst, seq). Because the firmware's `seq` is a
per-node uint8_t shared across all destinations, the same (src,dst,seq) can
recur on long runs (every ~256 packets to that destination). To stay correct
across wraps, we process events in chronological order and match each
`data_delivered` to the FIFO-oldest unmatched `data_sent` with the same key.
Sends still queued at end-of-run are losses; deliveries that find an empty
queue are reported as `orphan_deliveries` and do not affect PDR.

Latency = delivered.ts − send.ts of the matched pair.

Route stability per (src, dst): count via-hop changes in successive
`data_sent` events (or RTENTRY snapshots for non-source-local view).
"""

from __future__ import annotations

import json
import math
import statistics
from collections import defaultdict, deque
from pathlib import Path

# Allow direct script invocation from any cwd:
#   python3 scripts/testbed/analysis/routing.py <run_dir>
if __name__ == "__main__":
    import sys
    from pathlib import Path
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import Event, parse_run


def _flow_key(src: str, dst: str, seq: int) -> tuple[str, str, int]:
    return (src.upper(), dst.upper(), int(seq))


def _percentile(values: list[float], pct: float) -> float | None:
    if not values:
        return None
    s = sorted(values)
    if len(s) == 1:
        return s[0]
    rank = pct / 100 * (len(s) - 1)
    lo = math.floor(rank)
    hi = math.ceil(rank)
    if lo == hi:
        return s[lo]
    frac = rank - lo
    return s[lo] + frac * (s[hi] - s[lo])


def compute(events: list[Event]) -> dict:
    # FIFO queue of pending sends per (src, dst, seq). Each entry is the
    # data_sent Event. A delivery pops the oldest unmatched send with the
    # same key — robust to per-node seq wrap on long runs.
    pending: dict[tuple[str, str, int], deque[Event]] = defaultdict(deque)
    ttl_expired_count: int = 0
    no_route_count: int = 0
    orphan_deliveries: int = 0

    # Per-flow buckets, accumulated as events are matched (in time order).
    per_flow: dict[str, dict] = defaultdict(lambda: {"sent": 0, "delivered": 0,
                                                     "lats_ms": []})
    latencies: list[float] = []
    via_per_flow: dict[tuple[str, str], list[str]] = defaultdict(list)
    pkt_rx_rssi: list[float] = []
    pkt_rx_snr: list[float] = []

    for ev in events:
        if ev.kind == "data_sent":
            f = ev.fields
            dst = f["dst"]
            key = _flow_key(ev.node, dst, f["seq"])
            pending[key].append(ev)
            flow_id = f"{ev.node.upper()}->{dst.upper()}"
            per_flow[flow_id]["sent"] += 1
            via_per_flow[(ev.node, dst.upper())].append(f["via"].upper())
        elif ev.kind == "data_delivered":
            f = ev.fields
            src = f["src"]
            key = _flow_key(src, ev.node, f["seq"])
            queue = pending.get(key)
            if not queue:
                # Delivery with no outstanding send — usually means the send
                # log line was lost/never seen. Don't credit PDR.
                orphan_deliveries += 1
                continue
            sent_ev = queue.popleft()
            if not queue:
                del pending[key]
            flow_id = f"{src.upper()}->{ev.node.upper()}"
            per_flow[flow_id]["delivered"] += 1
            lat_ms = (ev.ts - sent_ev.ts) * 1000.0
            per_flow[flow_id]["lats_ms"].append(lat_ms)
            latencies.append(lat_ms)
        elif ev.kind == "data_ttl_expired":
            ttl_expired_count += 1
        elif ev.kind == "data_no_route":
            no_route_count += 1
        elif ev.kind == "pkt_rx":
            pkt_rx_rssi.append(ev.fields["rssi"])
            pkt_rx_snr.append(ev.fields["snr"])

    pdr_per_flow = {}
    for flow, b in per_flow.items():
        pdr_per_flow[flow] = {
            "sent": b["sent"],
            "delivered": b["delivered"],
            "pdr": (b["delivered"] / b["sent"]) if b["sent"] else None,
            "latency_ms": {
                "p50": _percentile(b["lats_ms"], 50),
                "p95": _percentile(b["lats_ms"], 95),
                "p99": _percentile(b["lats_ms"], 99),
                "mean": (statistics.fmean(b["lats_ms"]) if b["lats_ms"] else None),
                "n": len(b["lats_ms"]),
            },
        }

    # Route stability: changes in `via` for each (src, dst) flow.
    route_changes = {}
    for (src, dst), seq in via_per_flow.items():
        changes = sum(1 for a, b in zip(seq, seq[1:]) if a != b)
        route_changes[f"{src}->{dst}"] = {
            "transmits": len(seq),
            "via_changes": changes,
            "via_change_rate": (changes / max(len(seq) - 1, 1)) if len(seq) > 1 else 0.0,
        }

    total_sent = sum(b["sent"] for b in per_flow.values())
    total_delivered = sum(b["delivered"] for b in per_flow.values())

    return {
        "totals": {
            "sent": total_sent,
            "delivered": total_delivered,
            "ttl_expired": ttl_expired_count,
            "no_route": no_route_count,
            "orphan_deliveries": orphan_deliveries,
            "pdr": (total_delivered / total_sent) if total_sent else None,
        },
        "latency_ms_overall": {
            "p50": _percentile(latencies, 50),
            "p95": _percentile(latencies, 95),
            "p99": _percentile(latencies, 99),
            "mean": (statistics.fmean(latencies) if latencies else None),
            "n": len(latencies),
        },
        "pdr_per_flow": pdr_per_flow,
        "route_stability": route_changes,
        "link_rssi_dbm": {
            "mean": (statistics.fmean(pkt_rx_rssi) if pkt_rx_rssi else None),
            "min": (min(pkt_rx_rssi) if pkt_rx_rssi else None),
            "max": (max(pkt_rx_rssi) if pkt_rx_rssi else None),
            "n": len(pkt_rx_rssi),
        },
        "link_snr_db": {
            "mean": (statistics.fmean(pkt_rx_snr) if pkt_rx_snr else None),
            "min": (min(pkt_rx_snr) if pkt_rx_snr else None),
            "max": (max(pkt_rx_snr) if pkt_rx_snr else None),
            "n": len(pkt_rx_snr),
        },
    }


def analyse(run_dir: Path) -> dict:
    events = parse_run(run_dir)
    return compute(events)


def _selftest() -> None:
    """Synthetic check: two sends + two deliveries with the same (src,dst,seq)
    must both be counted (uint8_t seq wrap case)."""
    e = lambda ts, node, kind, fields: Event(ts=ts, node=node, kind=kind, fields=fields)
    events = [
        e(10.0,  "A001", "data_sent",      {"dst": "B002", "via": "B002", "ttl": 32, "seq": 42, "payload_size": 100}),
        e(11.0,  "B002", "data_delivered", {"src": "A001", "dest": "B002", "seq": 42, "payload_size": 100}),
        e(500.0, "A001", "data_sent",      {"dst": "B002", "via": "B002", "ttl": 32, "seq": 42, "payload_size": 100}),
        e(501.0, "B002", "data_delivered", {"src": "A001", "dest": "B002", "seq": 42, "payload_size": 100}),
    ]
    r = compute(events)
    assert r["totals"]["sent"] == 2,        f"sent={r['totals']['sent']}"
    assert r["totals"]["delivered"] == 2,   f"delivered={r['totals']['delivered']}"
    assert r["totals"]["pdr"] == 1.0,       f"pdr={r['totals']['pdr']}"
    assert r["totals"]["orphan_deliveries"] == 0
    lats = [r["pdr_per_flow"]["A001->B002"]["latency_ms"]["p50"]]
    assert abs(lats[0] - 1000.0) < 1e-6,    f"p50 latency={lats[0]}"
    print("routing._selftest OK")


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Compute routing metrics for one run")
    ap.add_argument("run_dir", type=Path, nargs="?",
                    help="(omit when using --selftest)")
    ap.add_argument("--selftest", action="store_true",
                    help="Run synthetic regression test for wrap-collision matching")
    args = ap.parse_args()
    if args.selftest:
        _selftest()
        return 0
    if args.run_dir is None:
        ap.error("run_dir required (or pass --selftest)")
    result = analyse(args.run_dir)
    out = args.run_dir / "routing.json"
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
