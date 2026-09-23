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

**Not every campaign has those lines.** They are emitted by the Network Manager
at a verbosity the `sim_load_compare` / `sim_size_compare` campaigns did not
capture: present in `dataslots_check` and `main_cluster_pdr`, absent from 0/3
sampled runs of each sim campaign. For those, `extract_slot_wrap()` recovers the
frame SIZE from a signal every node emits — the `Slot N transition` index, which
wraps at the superframe boundary. It cannot recover composition, only
`total_slots`, so it is an opt-in fallback (`allow_slot_wrap=True`) rather than
an automatic one: `aggregate_anchors` averages the composition fields and would
be poisoned by a record that lacks them.
"""

from __future__ import annotations

import json
import re
from collections import Counter
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

# ── Fallback: the per-node slot index, which wraps at the frame boundary ─────
# Same shape as parse_logs._PATTERNS["slot"]; matched here directly because this
# module scans raw logs (formation happens before the windowed event stream).
_RE_SLOT_IDX = re.compile(r"Slot (\d+) transition")
_WRAP_MIN_SLOTS = 100      # ignore nodes with too few transitions to see a wrap


def _load_config(run_dir: Path) -> dict:
    """Return the run's frozen LoRa config from config.yaml (defaults block)."""
    cfg_path = run_dir / "config.yaml"
    out = {"sf": None, "bw_khz": 125.0, "cr_denom": 7,
           "duty_cycle": None, "data_slots": 1, "max_data_slots": None}
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
        elif key == "lora_max_data_slots":
            out["max_data_slots"] = int(float(val))
    return out


def extract_slot_wrap(run_dir: Path) -> dict | None:
    """Recover `total_slots` from `Slot N transition` index wraps. None if absent.

    Fallback for campaigns with no NM slot-budget lines (see module docstring).
    The slot index counts 0..total-1 and wraps at the frame boundary, so the
    index seen immediately before a wrap, plus one, IS the frame size — measured,
    with no model assumption.

    Two things make this honest rather than approximate:
      - The frame GROWS as nodes join, so early wraps report a smaller frame.
        We use only the late half of each node's stream (post-formation).
      - A dropped log line fakes a wrap. We take the MODE across all nodes and
        report the modal share as `confidence`; a low share means the frame never
        settled and the caller should not trust it.

    Returns `total_slots`, `n_nodes`, `confidence`, `method`. Composition
    (sync/control/discovery/data) is NOT recoverable this way and is absent —
    deliberately, rather than invented.
    """
    logs_dir = run_dir / "logs"
    if not logs_dir.is_dir():
        return None

    sizes: list[int] = []
    n_nodes = 0
    for path in sorted(logs_dir.glob("monitor-dev-*.log*")):
        idx: list[int] = []
        try:
            with _open_log(path) as fh:
                for raw in fh:
                    m = _TS_RE.match(raw)
                    line = _strip_ansi(m.group(2)) if m else raw
                    if (mm := _RE_SLOT_IDX.search(line)):
                        idx.append(int(mm.group(1)))
        except OSError:
            continue
        if len(idx) < _WRAP_MIN_SLOTS:
            continue
        n_nodes += 1
        late = idx[len(idx) // 2:]
        sizes += [late[i - 1] + 1 for i in range(1, len(late))
                  if late[i] < late[i - 1]]

    if not sizes:
        return None
    counts = Counter(sizes)
    total, hits = counts.most_common(1)[0]
    return {
        "run": run_dir.name,
        "method": "slot_wrap",
        "total_slots": total,
        "n_nodes": n_nodes,
        "confidence": hits / len(sizes),
        "wraps_seen": len(sizes),
    }


def extract(run_dir: Path, allow_slot_wrap: bool = False) -> dict | None:
    """Parse converged superframe structure from a run's logs. None if absent.

    `allow_slot_wrap` opts into the `extract_slot_wrap()` fallback when the NM
    lines are missing. It is OFF by default because the fallback cannot recover
    composition, and `aggregate_anchors` averages those fields.
    """
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
        return extract_slot_wrap(run_dir) if allow_slot_wrap else None

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
        "max_data_slots_cfg": cfg["max_data_slots"],
        "slot_dur_ms": slot_dur_ms, "toa_ms": toa_ms,
        "max_packet_bytes": max_packet, "guard_ms": guard_ms,
        "total_slots": total, "sync": sync, "control": control,
        "discovery": discovery, "data": data, "sleep": sleep,
        "node_count": node_count, "max_hops": max_hops,
        "data_slots_granted": per_node_data,
        "superframe_s": superframe_s, "overhead_frac": overhead_frac,
        "capacity_Bps": capacity_Bps, "recurrence_s": recurrence_s,
    }


# ── Multi-batch, (SF, data_slots)-keyed aggregation ──────────────────────────

# Numeric fields averaged across the reps of one (SF, data_slots) cell.
_ANCHOR_NUM_KEYS = [
    "total_slots", "sync", "control", "discovery", "data", "sleep",
    "slot_dur_ms", "max_packet_bytes", "superframe_s", "overhead_frac",
    "capacity_Bps", "node_count", "max_hops", "data_slots_granted",
    "recurrence_s", "duty_cycle",
]


def aggregate_anchors(batch_dirs, drop_first: int = 1,
                      require_full_membership: bool = True,
                      frame_selection: str = "mean") -> dict:
    """Mean superframe structure per (SF, data_slots) across one or more batches.

    Unlike ``plot_superframe.aggregate_measured`` (which keys by SF only and
    derives the SF from the cell name), this reads the SF and data-slot count
    from each run's own extracted record, so it copes with cells named by the
    knob under test (e.g. ``Slots2`` / ``Slots4``) and merges runs from several
    batch directories into a single design-space picture.

    Args:
        batch_dirs: iterable of batch directories (each holds run subdirs).
        drop_first: skip reps with index < this (the r00 warm-up).
        require_full_membership: per SF, keep only runs whose converged
            node_count equals the maximum observed for that SF, so every
            data-slot anchor is compared at the same network size (drops the
            partial-join tail runs where a node had not joined by window end).
        frame_selection: ``"mean"`` averages every run in the cell; ``"modal"``
            first keeps only the runs sharing the cell's most common
            ``total_slots``. Runs at the same node count but different hop
            depth build different-sized frames, and averaging those yields a
            composition no run ever ran (e.g. 3.6 sync slots). ``"modal"``
            reports one real converged schedule instead.

    Returns:
        {(sf, ds): {<mean fields>, "n", "node_count_max", "max_data_slots_cfg",
                    "runs": [record, ...]}}
        keyed by (spreading factor, rounded data_slots_granted).
    """
    from collections import defaultdict
    from statistics import mean

    records: list[dict] = []
    for batch_dir in batch_dirs:
        batch_dir = Path(batch_dir)
        if not batch_dir.is_dir():
            continue
        for run_dir in sorted(batch_dir.iterdir()):
            if not run_dir.is_dir():
                continue
            cr = _cell_of(run_dir)
            if cr is None:
                continue
            _cell, rep = cr
            if rep < drop_first:
                continue
            rec = extract(run_dir)
            if rec and rec.get("sf") is not None:
                rec["batch"] = batch_dir.name
                records.append(rec)

    # Per-SF full-membership target (max converged node_count seen for that SF).
    sf_target: dict[int, int] = defaultdict(int)
    for r in records:
        sf_target[r["sf"]] = max(sf_target[r["sf"]], r["node_count"])

    groups: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for r in records:
        if require_full_membership and r["node_count"] != sf_target[r["sf"]]:
            continue
        ds = int(round(r["data_slots_granted"]))
        groups[(r["sf"], ds)].append(r)

    if frame_selection not in ("mean", "modal"):
        raise ValueError(f"frame_selection must be 'mean' or 'modal', "
                         f"got {frame_selection!r}")

    out: dict[tuple[int, int], dict] = {}
    for key, recs in groups.items():
        if frame_selection == "modal":
            modal_slots = Counter(r["total_slots"] for r in recs).most_common(1)[0][0]
            recs = [r for r in recs if r["total_slots"] == modal_slots]
        agg = {k: mean(r[k] for r in recs if r[k] is not None)
               for k in _ANCHOR_NUM_KEYS}
        agg["n"] = len(recs)
        agg["node_count_max"] = sf_target[key[0]]
        # Pool size the firmware actually ran with (default 50 if unset in cfg).
        pools = [r["max_data_slots_cfg"] for r in recs
                 if r.get("max_data_slots_cfg") is not None]
        agg["max_data_slots_cfg"] = max(pools) if pools else None
        agg["runs"] = recs
        out[key] = agg
    return out


# Lazy import to avoid a hard dependency cycle at module load.
def _cell_of(run_dir: Path):
    from analysis.multirun import _cell_of as _c
    return _c(run_dir)


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Extract converged superframe structure")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--allow-slot-wrap", action="store_true",
                    help="if the NM slot-budget lines are absent (the sim "
                         "campaigns), recover total_slots from Slot-index wraps. "
                         "Yields no composition — total_slots only.")
    args = ap.parse_args()
    res = extract(args.run_dir, allow_slot_wrap=args.allow_slot_wrap)
    if res is None:
        print(f"no superframe-config lines found in {args.run_dir}"
              + ("" if args.allow_slot_wrap else " (try --allow-slot-wrap)"))
        return 1
    out = args.run_dir / "superframe.json"
    out.write_text(json.dumps(res, indent=2, sort_keys=True))
    print(json.dumps(res, indent=2, sort_keys=True))
    print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
