"""Extract the converged TDMA superframe structure from one run's logs.

The Network Manager logs its slot budget every time the superframe is rebuilt
(network_service.cpp), e.g.:

    Auto-calculated slot duration: 2850 ms (ToA(51)=2728 ms + guard=50 ms + margin)
    Total slots in the superframe 52 (target TX duty cycle: 10.00%)
    Active slots 50: sync 6, control 16, discovery 12, data 16
    SLEEP slots 2 | actual TX duty cycle: 4.45%

These lines appear during formation — BEFORE the measurement window — so we scan
the full log, not the windowed event stream, and keep the LAST (converged) value
of each. The result is the per-run anchor that validates analysis/model.py:
`control` = node_count, `sync-1` = max_hops, and total = active + churn.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import _TS_RE, _open_log, _strip_ansi

# ── Regexes for the NM superframe-config lines ───────────────────────────────
_RE_SLOTDUR = re.compile(
    r"Auto-calculated slot duration:\s*(\d+)\s*ms\s*\(ToA\((\d+)\)=(\d+)\s*ms"
    r"\s*\+\s*guard=(\d+)\s*ms")
_RE_TOTAL = re.compile(r"Total slots in the superframe\s+(\d+)")
_RE_ACTIVE = re.compile(
    r"Active slots\s+(\d+):\s*sync\s+(\d+),\s*control\s+(\d+),"
    r"\s*discovery\s+(\d+),\s*data\s+(\d+)")
_RE_SLEEP = re.compile(r"SLEEP slots\s+(\d+)")


def _load_config(run_dir: Path) -> dict:
    """Return the run's frozen LoRa config from config.yaml (defaults block)."""
    cfg_path = run_dir / "config.yaml"
    out = {"sf": None, "bw_khz": 125.0, "cr_denom": 7,
           "duty_cycle": None, "data_slots": 1}
    if not cfg_path.is_file():
        return out
    # Tiny YAML reader — config.yaml is flat key: value under `defaults:`.
    # Avoids a PyYAML dependency the other analysis modules don't carry.
    in_defaults = False
    for line in cfg_path.read_text().splitlines():
        if re.match(r"^\S", line):
            in_defaults = line.strip().startswith("defaults:")
            continue
        if not in_defaults:
            continue
        m = re.match(r"\s+([\w]+):\s*([\d.]+)", line)
        if not m:
            continue
        key, val = m.group(1), m.group(2)
        if key == "lora_spreading_factor":
            out["sf"] = int(float(val))
        elif key == "lora_bandwidth":
            out["bw_khz"] = float(val)
        elif key == "lora_coding_rate":
            out["cr_denom"] = int(float(val))
        elif key == "lora_duty_cycle":
            out["duty_cycle"] = float(val)
        elif key == "lora_default_data_slots":
            out["data_slots"] = int(float(val))
    return out


def extract(run_dir: Path) -> dict | None:
    """Parse converged superframe structure from a run's logs. None if absent."""
    logs_dir = run_dir / "logs"
    if not logs_dir.is_dir():
        return None

    slotdur = total = active = sleep = None
    for path in sorted(logs_dir.glob("monitor-dev-*.log*")):
        try:
            with _open_log(path) as fh:
                for raw in fh:
                    m = _TS_RE.match(raw)
                    line = _strip_ansi(m.group(2)) if m else raw
                    if (mm := _RE_SLOTDUR.search(line)):
                        slotdur = tuple(int(g) for g in mm.groups())  # dur, maxpkt, toa, guard
                    elif (mm := _RE_TOTAL.search(line)):
                        total = int(mm.group(1))
                    elif (mm := _RE_ACTIVE.search(line)):
                        active = tuple(int(g) for g in mm.groups())   # active,sync,control,discovery,data
                    elif (mm := _RE_SLEEP.search(line)):
                        sleep = int(mm.group(1))
        except OSError:
            continue

    if slotdur is None or total is None or active is None:
        return None

    slot_dur_ms, max_packet, toa_ms, guard_ms = slotdur
    _active, sync, control, discovery, data = active
    node_count = control                       # control slot per node
    max_hops = max(sync - 1, 0)
    per_node_data = data / node_count if node_count else 0.0
    superframe_s = total * slot_dur_ms / 1000.0
    overhead_frac = ((sync + control + discovery) / total) if total else 0.0
    capacity_Bps = (per_node_data * max_packet / superframe_s) if superframe_s else 0.0
    recurrence_s = (superframe_s / per_node_data) if per_node_data else None

    cfg = _load_config(run_dir)
    return {
        "run": run_dir.name,
        "sf": cfg["sf"], "bw_khz": cfg["bw_khz"], "cr_denom": cfg["cr_denom"],
        "duty_cycle": cfg["duty_cycle"], "data_slots_cfg": cfg["data_slots"],
        "slot_dur_ms": slot_dur_ms, "toa_ms": toa_ms,
        "max_packet_bytes": max_packet, "guard_ms": guard_ms,
        "total_slots": total, "sync": sync, "control": control,
        "discovery": discovery, "data": data, "sleep": sleep,
        "node_count": node_count, "max_hops": max_hops,
        "data_slots_granted": per_node_data,
        "superframe_s": superframe_s, "overhead_frac": overhead_frac,
        "capacity_Bps": capacity_Bps, "recurrence_s": recurrence_s,
    }


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Extract converged superframe structure")
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    res = extract(args.run_dir)
    if res is None:
        print(f"no superframe-config lines found in {args.run_dir}")
        return 1
    out = args.run_dir / "superframe.json"
    out.write_text(json.dumps(res, indent=2, sort_keys=True))
    print(json.dumps(res, indent=2, sort_keys=True))
    print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
