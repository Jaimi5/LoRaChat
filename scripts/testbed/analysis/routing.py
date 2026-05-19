"""Routing performance: PDR, end-to-end latency, hop distribution, route stability.

Match key for a flow: (src, dst, seq). PDR is computed by matching every
`data_sent` event against the first subsequent `data_delivered` event with
the same key. Latency = delivered.ts − sent.ts.

Route stability per (src, dst): count via-hop changes in successive
`data_sent` events (or RTENTRY snapshots for non-source-local view).
"""

from __future__ import annotations

import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

from .parse_logs import Event, parse_run


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
    sent: dict[tuple[str, str, int], Event] = {}
    delivered: dict[tuple[str, str, int], Event] = {}
    ttl_expired: set[tuple[str, str, int]] = set()
    no_route: set[tuple[str, str, int]] = set()

    via_per_flow: dict[tuple[str, str], list[str]] = defaultdict(list)
    pkt_rx_rssi: list[float] = []
    pkt_rx_snr: list[float] = []

    for ev in events:
        if ev.kind == "data_sent":
            f = ev.fields
            key = _flow_key(ev.node, f["dst"], f["seq"])
            # Earliest send wins (a node only sends once per (dst, seq)).
            sent.setdefault(key, ev)
            via_per_flow[(ev.node, f["dst"].upper())].append(f["via"].upper())
        elif ev.kind == "data_delivered":
            f = ev.fields
            key = _flow_key(f["src"], ev.node, f["seq"])
            delivered.setdefault(key, ev)
        elif ev.kind == "data_ttl_expired":
            f = ev.fields
            ttl_expired.add(_flow_key(f["src"], f["dst"], f["seq"]))
        elif ev.kind == "data_no_route":
            f = ev.fields
            no_route.add(_flow_key(f["src"], f["dst"], f["seq"]))
        elif ev.kind == "pkt_rx":
            pkt_rx_rssi.append(ev.fields["rssi"])
            pkt_rx_snr.append(ev.fields["snr"])

    # Per-flow PDR + latency aggregates.
    per_flow: dict[str, dict] = {}
    latencies: list[float] = []
    for key, sent_ev in sent.items():
        src, dst, seq = key
        dev_ev = delivered.get(key)
        flow_id = f"{src}->{dst}"
        bucket = per_flow.setdefault(flow_id, {"sent": 0, "delivered": 0,
                                               "lats_ms": []})
        bucket["sent"] += 1
        if dev_ev is not None:
            bucket["delivered"] += 1
            lat_ms = (dev_ev.ts - sent_ev.ts) * 1000.0
            bucket["lats_ms"].append(lat_ms)
            latencies.append(lat_ms)

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
            "ttl_expired": len(ttl_expired),
            "no_route": len(no_route),
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


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Compute routing metrics for one run")
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    result = analyse(args.run_dir)
    out = args.run_dir / "routing.json"
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
