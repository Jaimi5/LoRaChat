"""Application-layer end-to-end PDR / goodput / latency from APP_TX / APP_RX.

Version-neutral by construction: the firmware emits `APP_TX` at the originator
and `APP_RX` at the final destination, *above* the LoRaMesher library, so v1 and
v2 are measured identically (the internal mesh logs differ and are not used).

This is the metric for the sim-based v1-vs-v2 load-generator comparison
(`batches/sim_load_compare.yaml`). The sim sends a fixed burst of fixed-size
packets tagged with a per-burst index `seq` (uint8_t messageId), so the match
key is (src, seq). Like `routing.compute`, we process events in time order and
FIFO-match each `APP_RX` to the oldest unmatched `APP_TX` with the same key,
which stays correct if `seq` ever wraps. APP_TX with no matching APP_RX = loss;
APP_RX with no outstanding APP_TX = orphan (its TX log was lost) and is excluded
from PDR.

    PDR     = delivered / sent
    goodput = delivered_bytes / window_s   (bytes = LoRaMesher payload in APP_TX)
    latency = rx.ts − tx.ts
"""

from __future__ import annotations

import json
import math
import statistics
from collections import defaultdict, deque
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import Event, parse_run

SIM_APP = 12  # appPort::SimApp — the load generator's source app port.


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


def compute(events: list[Event], window_s: float | None = None,
            app_filter: int | None = SIM_APP) -> dict:
    # FIFO queue of pending APP_TX per (src, seq); an APP_RX pops the oldest.
    pending: dict[tuple[str, int], deque[Event]] = defaultdict(deque)
    per_flow: dict[str, dict] = defaultdict(
        lambda: {"sent": 0, "delivered": 0, "delivered_bytes": 0, "lats_ms": []})
    latencies: list[float] = []
    orphan_deliveries = 0
    ts_min = ts_max = None

    for ev in events:
        ts_min = ev.ts if ts_min is None else min(ts_min, ev.ts)
        ts_max = ev.ts if ts_max is None else max(ts_max, ev.ts)
        if ev.kind == "app_tx":
            f = ev.fields
            src = f["src"].upper()
            pending[(src, int(f["seq"]))].append(ev)
            per_flow[src]["sent"] += 1
        elif ev.kind == "app_rx":
            f = ev.fields
            if app_filter is not None and f.get("app") != app_filter:
                continue
            src = f["src"].upper()
            queue = pending.get((src, int(f["seq"])))
            if not queue:
                # Delivery with no outstanding send — the TX log line was lost.
                orphan_deliveries += 1
                continue
            tx = queue.popleft()
            if not queue:
                del pending[(src, int(f["seq"]))]
            per_flow[src]["delivered"] += 1
            per_flow[src]["delivered_bytes"] += int(tx.fields.get("size", 0))
            lat_ms = (ev.ts - tx.ts) * 1000.0
            per_flow[src]["lats_ms"].append(lat_ms)
            latencies.append(lat_ms)

    if not window_s and ts_min is not None and ts_max is not None:
        window_s = max(ts_max - ts_min, 1.0)

    pdr_per_flow = {}
    for src, b in per_flow.items():
        pdr_per_flow[src] = {
            "sent": b["sent"],
            "delivered": b["delivered"],
            "pdr": (b["delivered"] / b["sent"]) if b["sent"] else None,
            "goodput_Bps": (b["delivered_bytes"] / window_s) if window_s else None,
            "latency_ms": {
                "p50": _percentile(b["lats_ms"], 50),
                "p95": _percentile(b["lats_ms"], 95),
                "p99": _percentile(b["lats_ms"], 99),
                "mean": (statistics.fmean(b["lats_ms"]) if b["lats_ms"] else None),
                "n": len(b["lats_ms"]),
            },
        }

    total_sent = sum(b["sent"] for b in per_flow.values())
    total_delivered = sum(b["delivered"] for b in per_flow.values())
    total_bytes = sum(b["delivered_bytes"] for b in per_flow.values())

    return {
        "totals": {
            "sent": total_sent,
            "delivered": total_delivered,
            "orphan_deliveries": orphan_deliveries,
            "pdr": (total_delivered / total_sent) if total_sent else None,
            "goodput_Bps": (total_bytes / window_s) if window_s else None,
            "n_senders": len(per_flow),
            "window_s": window_s,
        },
        "latency_ms_overall": {
            "p50": _percentile(latencies, 50),
            "p95": _percentile(latencies, 95),
            "p99": _percentile(latencies, 99),
            "mean": (statistics.fmean(latencies) if latencies else None),
            "n": len(latencies),
        },
        "pdr_per_flow": pdr_per_flow,
    }


def analyse(run_dir: Path) -> dict:
    # Analyse the FULL log, not the host measurement window. In SIM_PDR_COMPARE
    # mode the simulator emits only the self-contained burst (no background app
    # traffic), so windowing buys nothing and would force the firmware burst to be
    # aligned with run_batch's window. Skipping it decouples the two warmups:
    # `sim_testbed_warmup_ms` (firmware: when to start sending) and `warmup_min`
    # (host: run bookkeeping) only need to each exceed convergence — not match.
    # goodput's window_s falls back to the observed first-TX..last-RX span.
    events = parse_run(run_dir, window=(None, None))
    return compute(events, window_s=None)


def _selftest() -> None:
    """Synthetic check: delivery, loss, orphan, uint8 seq wrap, goodput."""
    e = lambda ts, node, kind, fields: Event(ts=ts, node=node, kind=kind, fields=fields)
    events = [
        # A001 -> sink GW01, seq 0 delivered after 1.0s, 40B payload
        e(10.0, "A001", "app_tx", {"src": "A001", "seq": 0, "size": 40}),
        e(11.0, "GW01", "app_rx", {"src": "A001", "seq": 0, "app": SIM_APP}),
        # seq 1 sent but never delivered -> loss
        e(12.0, "A001", "app_tx", {"src": "A001", "seq": 1, "size": 40}),
        # wrap re-use of seq 0 much later, delivered after 2.0s
        e(500.0, "A001", "app_tx", {"src": "A001", "seq": 0, "size": 40}),
        e(502.0, "GW01", "app_rx", {"src": "A001", "seq": 0, "app": SIM_APP}),
        # an RX for a different app port must be ignored
        e(503.0, "GW01", "app_rx", {"src": "A001", "seq": 9, "app": 16}),
        # orphan delivery (no matching TX) must not credit PDR
        e(504.0, "GW01", "app_rx", {"src": "B002", "seq": 7, "app": SIM_APP}),
    ]
    r = compute(events, window_s=494.0)
    assert r["totals"]["sent"] == 3, r["totals"]
    assert r["totals"]["delivered"] == 2, r["totals"]
    assert abs(r["totals"]["pdr"] - 2 / 3) < 1e-9, r["totals"]
    assert r["totals"]["orphan_deliveries"] == 1, r["totals"]
    # 2 delivered * 40B over 494s window
    assert abs(r["totals"]["goodput_Bps"] - 80.0 / 494.0) < 1e-9, r["totals"]
    assert abs(r["pdr_per_flow"]["A001"]["latency_ms"]["p50"] - 1500.0) < 1e-6, \
        r["pdr_per_flow"]["A001"]
    print("app_pdr._selftest OK")


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(
        description="Application-layer end-to-end PDR/goodput/latency for one run")
    ap.add_argument("run_dir", type=Path, nargs="?", help="(omit when using --selftest)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        _selftest()
        return 0
    if args.run_dir is None:
        ap.error("run_dir required (or pass --selftest)")
    result = analyse(args.run_dir)
    out = args.run_dir / "app_pdr.json"
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(json.dumps(result, indent=2, sort_keys=True))
    print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
