"""Per-node duty cycle from `Slot N transition` events; airtime-derived energy.

Sums slot durations grouped by `type` (TX / RX / SLEEP / DISCOVERY_* /
CONTROL_* / SYNC_BEACON_*). When more than one slot transition is observed,
slot duration is inferred as the gap to the next transition on the same
node.

The energy model is intentionally simple — datasheet currents per state
multiplied by measured time. Calibrate by replacing CURRENT_MA values with
your specific board's measurements (PPK2 spot validation).
"""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path

# Allow direct script invocation from any cwd:
#   python3 scripts/testbed/analysis/duty_cycle.py <run_dir>
if __name__ == "__main__":
    import sys
    from pathlib import Path
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import Event, parse_run


# Rough datasheet currents for an ESP32 + SX1276 module at SF9, 17 dBm TX,
# 3.3 V supply. Replace with your PPK2-calibrated numbers for accuracy.
CURRENT_MA = {
    "TX": 120.0,
    "RX": 12.0,
    "SLEEP": 1.0,
    "IDLE": 5.0,
    "DISCOVERY_TX": 120.0,
    "DISCOVERY_RX": 12.0,
    "CONTROL_TX": 120.0,
    "CONTROL_RX": 12.0,
    "SYNC_BEACON_TX": 120.0,
    "SYNC_BEACON_RX": 12.0,
}
VOLTAGE_V = 3.3


def _bucket(type_str: str) -> str:
    """Normalize the analyzer's slot type labels to bucket keys above."""
    t = type_str.upper()
    return t if t in CURRENT_MA else t  # unknown → carried verbatim, counted as 0 mA


def compute(events: list[Event]) -> dict:
    # Per-node, ordered list of (ts, type_bucket) slot transitions.
    by_node: dict[str, list[tuple[float, str]]] = defaultdict(list)
    for ev in events:
        if ev.kind != "slot":
            continue
        by_node[ev.node].append((ev.ts, _bucket(ev.fields["type"])))

    per_node = {}
    for node, transitions in by_node.items():
        if len(transitions) < 2:
            per_node[node] = {"slots": len(transitions), "ms_by_type": {}}
            continue
        ms_by_type: dict[str, float] = defaultdict(float)
        for (t0, kind), (t1, _next) in zip(transitions, transitions[1:]):
            dur_ms = (t1 - t0) * 1000.0
            if dur_ms <= 0:
                continue
            ms_by_type[kind] += dur_ms
        total_ms = sum(ms_by_type.values()) or 1.0
        pct_by_type = {k: 100.0 * v / total_ms for k, v in ms_by_type.items()}
        mAs = 0.0
        for kind, dur_ms in ms_by_type.items():
            mAs += CURRENT_MA.get(kind, 0.0) * (dur_ms / 1000.0)
        energy_mJ = mAs * VOLTAGE_V
        per_node[node] = {
            "slots": len(transitions),
            "ms_by_type": dict(ms_by_type),
            "pct_by_type": pct_by_type,
            "window_ms": total_ms,
            "energy_mJ": energy_mJ,
            "mean_current_mA": (mAs / (total_ms / 1000.0)) if total_ms else None,
        }
    return {
        "voltage_v": VOLTAGE_V,
        "current_mA_table": CURRENT_MA,
        "per_node": per_node,
    }


def analyse(run_dir: Path) -> dict:
    events = parse_run(run_dir)
    return compute(events)


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Compute duty cycle / energy model for one run")
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    result = analyse(args.run_dir)
    out = args.run_dir / "duty_cycle.json"
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
