"""Per-node duty cycle and airtime-derived energy, for both LoRaMesher versions.

Two estimation methods, one current table:

  - **v2 (TDMA), `method="slot_integration"`** — sums slot durations grouped by
    `type` (TX / RX / SLEEP / DISCOVERY_* / CONTROL_* / SYNC_BEACON_*) from
    `Slot N transition` events; slot duration is the gap to the next transition
    on the same node. This is the measured per-slot occupancy.

  - **v1 (Previous LoRaMesher — CSMA/CA distance-vector), `method="v1_continuous_rx"`** —
    v1 emits no slot events and never sleeps: the radio is in continuous RX (`LORA_DUTY_CYCLE=1.0`,
    `receivingRoutine()` re-arms RX after every TX/RX), interrupted only by its
    own transmissions. So its energy is the continuous-RX floor plus the airtime
    it actually spent transmitting:

        T_tx   = Σ_sends  ToA(SF, size)             # from "Packet send -- Size: N"
        mean_I = ( I_RX·(W − T_tx) + I_TX·T_tx ) / W

    `W` is the lifecycle measurement window; `T_tx` reuses `model.toa_ms`, the
    same airtime formula the v2 slot budget is built on. This keeps the v1 vs v2
    energy comparison fair: one current table, one airtime formula.

The energy model is intentionally simple — per-state currents multiplied by
time. The currents themselves live in `analysis/radio_power.py`, keyed on each
node's configured `lora_power`; see that module for provenance and for why a
flat TX current was wrong. Pass `source="legacy"` to reproduce the pre-fix
numbers exactly.
"""

from __future__ import annotations

import json
import sys
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

from analysis.parse_logs import Event, parse_run, load_measurement_window
from analysis.model import toa_ms
from analysis.radio_power import (DEFAULT_TX_DBM, SOURCES, VOLTAGE_V,
                                  current_table, describe)


def _bucket(type_str: str) -> str:
    """Normalize the analyzer's slot type labels to current-table keys."""
    return type_str.upper()


def _compute_slot_node(transitions: list[tuple[float, str]],
                       table: dict[str, float]) -> dict:
    """v2 path: integrate slot durations × per-state current for one node."""
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
        # An unrecognised slot type counts as drawing nothing.
        mAs += table.get(kind, 0.0) * (dur_ms / 1000.0)
    return {
        "method": "slot_integration",
        "slots": len(transitions),
        "ms_by_type": dict(ms_by_type),
        "pct_by_type": pct_by_type,
        "window_ms": total_ms,
        "energy_mJ": mAs * VOLTAGE_V,
        "mean_current_mA": (mAs / (total_ms / 1000.0)) if total_ms else None,
    }


def _compute_v1_node(sizes: list[int], window_s: float, sf: int,
                     bw_khz: float, cr_denom: int,
                     table: dict[str, float]) -> dict:
    """v1 path: continuous RX over the window + measured TX airtime.

    `sizes` is the PHY byte size of every "Packet send" the node logged in the
    window. The radio is in RX the whole window except while transmitting.
    """
    tx_s = sum(toa_ms(sf, max(s, 1), bw_khz, cr_denom) for s in sizes) / 1000.0
    tx_s = min(tx_s, window_s)            # guard: airtime cannot exceed window
    rx_s = max(window_s - tx_s, 0.0)
    mAs = table["TX"] * tx_s + table["RX"] * rx_s
    return {
        "method": "v1_continuous_rx",
        "slots": 0,
        "n_tx": len(sizes),
        "tx_airtime_s": tx_s,
        "ms_by_type": {"TX": tx_s * 1000.0, "RX": rx_s * 1000.0},
        "pct_by_type": {"TX": 100.0 * tx_s / window_s if window_s else 0.0,
                        "RX": 100.0 * rx_s / window_s if window_s else 0.0},
        "window_ms": window_s * 1000.0,
        "energy_mJ": mAs * VOLTAGE_V,
        "mean_current_mA": (mAs / window_s) if window_s else None,
    }


def compute(events: list[Event], window_s: float | None = None,
            sf: int | None = None, bw_khz: float = 125.0,
            cr_denom: int = 7,
            power_by_node: dict[str, float] | None = None,
            default_dbm: float = DEFAULT_TX_DBM,
            source: str = "best") -> dict:
    """Per-node energy. v2 nodes use slot integration; v1 nodes (no slot events
    but with `v1_pkt_send`) use the continuous-RX model when `sf`/`window_s` are
    available.

    Both paths resolve the same table for the same node, which is what keeps the
    v1-vs-v2 comparison on a common footing.
    """
    power_by_node = power_by_node or {}
    # Per-node ordered slot transitions, and per-node v1 TX sizes.
    by_node: dict[str, list[tuple[float, str]]] = defaultdict(list)
    v1_sends: dict[str, list[int]] = defaultdict(list)
    ts_min = ts_max = None
    for ev in events:
        ts_min = ev.ts if ts_min is None else min(ts_min, ev.ts)
        ts_max = ev.ts if ts_max is None else max(ts_max, ev.ts)
        if ev.kind == "slot":
            by_node[ev.node].append((ev.ts, _bucket(ev.fields["type"])))
        elif ev.kind == "v1_pkt_send":
            v1_sends[ev.node].append(int(ev.fields.get("size", 0)))

    # v1 window: prefer the lifecycle measurement window; else the event span.
    if window_s is None and ts_min is not None and ts_max is not None:
        window_s = max(ts_max - ts_min, 1.0)

    per_node: dict[str, dict] = {}
    fallback_nodes: list[str] = []
    for node in sorted(set(by_node) | set(v1_sends)):
        if node not in power_by_node:
            fallback_nodes.append(node)
        dbm = power_by_node.get(node, default_dbm)
        table = current_table(dbm, source=source)
        prov = describe(dbm, source=source)
        transitions = by_node.get(node, [])
        if len(transitions) >= 2:
            per_node[node] = _compute_slot_node(transitions, table)
        elif v1_sends.get(node) and sf is not None and window_s:
            per_node[node] = _compute_v1_node(v1_sends[node], window_s, sf,
                                              bw_khz, cr_denom, table)
        else:
            # Not enough information for either model.
            per_node[node] = {"slots": len(transitions), "ms_by_type": {}}
        per_node[node].update(prov)

    result = {
        "voltage_v": VOLTAGE_V,
        "source": source,
        # Per-node currents now vary with lora_power, so a single top-level
        # table would be a lie. This is the default-power table for reference;
        # each per_node entry carries the currents actually applied to it.
        "current_mA_table_default": current_table(default_dbm, source=source),
        "default_tx_power_dbm": default_dbm,
        "window_s": window_s,
        "per_node": per_node,
        "power_fallback_nodes": sorted(fallback_nodes),
    }

    # Observability guard: a node that logged activity but has no `lora_power` in
    # the run's config falls back to `default_dbm`. That is benign when the config
    # set `defaults.lora_power`, but when `default_dbm` is the compiled-in
    # DEFAULT_TX_DBM (no per-node AND no defaults entry) the node is charged at a
    # power *no testbed node actually runs at* (see radio_power.py) — the tell-tale
    # of config/serial-label drift. Surface it instead of silently mischarging.
    if fallback_nodes and default_dbm == DEFAULT_TX_DBM:
        warning = (
            f"lora_power missing from config for {sorted(fallback_nodes)}; "
            f"charged at compiled-in DEFAULT_TX_DBM={DEFAULT_TX_DBM} dBm, which no "
            "testbed node runs at. Check for config/serial-label drift.")
        print(f"WARNING [duty_cycle]: {warning}", file=sys.stderr)
        result["warnings"] = [warning]

    return result


def _read_node_power(run_dir: Path) -> tuple[dict[str, float], float]:
    """Return ({node_id: lora_power dBm}, default_dbm) from the run's config.yaml.

    Node ids are upper-cased to match `parse_logs.short_id_from_filename`; YAML
    parses an all-digit device id (e.g. 3428) as an int, hence the str() first.

    A node with no `lora_power` falls back to `defaults.lora_power` if present,
    else to the firmware's compiled-in LORA_POWER — which is what the config
    tool's omit-the-flag behaviour actually produces.
    """
    cfg_path = run_dir / "config.yaml"
    if not cfg_path.is_file():
        return ({}, DEFAULT_TX_DBM)
    try:
        import yaml
        cfg = yaml.safe_load(cfg_path.read_text()) or {}
    except Exception:
        return ({}, DEFAULT_TX_DBM)
    if not isinstance(cfg, dict):
        return ({}, DEFAULT_TX_DBM)

    default_dbm = DEFAULT_TX_DBM
    d = cfg.get("defaults") or {}
    if isinstance(d, dict) and d.get("lora_power") is not None:
        try:
            default_dbm = float(d["lora_power"])
        except (TypeError, ValueError):
            pass

    out: dict[str, float] = {}
    devices = cfg.get("devices") or {}
    if isinstance(devices, dict):
        for node, dev in devices.items():
            if not isinstance(dev, dict) or dev.get("lora_power") is None:
                continue
            try:
                out[str(node).strip().upper()] = float(dev["lora_power"])
            except (TypeError, ValueError):
                continue
    return (out, default_dbm)


def _read_lora_config(run_dir: Path) -> tuple[int | None, float, int]:
    """Return (sf, bw_khz, cr_denom) from the run's config.yaml defaults."""
    cfg_path = run_dir / "config.yaml"
    if not cfg_path.is_file():
        return (None, 125.0, 7)
    try:
        import yaml
        cfg = yaml.safe_load(cfg_path.read_text()) or {}
    except Exception:
        return (None, 125.0, 7)
    d = cfg.get("defaults", {}) if isinstance(cfg, dict) else {}
    sf = d.get("lora_spreading_factor")
    bw = d.get("lora_bandwidth", 125.0)
    cr = d.get("lora_coding_rate", 7)
    try:
        return (int(sf) if sf is not None else None, float(bw), int(cr))
    except (TypeError, ValueError):
        return (None, 125.0, 7)


def analyse(run_dir: Path, source: str = "best") -> dict:
    events = parse_run(run_dir)
    sf, bw_khz, cr_denom = _read_lora_config(run_dir)
    power_by_node, default_dbm = _read_node_power(run_dir)
    start, end = load_measurement_window(run_dir)
    window_s = (end - start) if (start is not None and end is not None) else None
    return compute(events, window_s=window_s, sf=sf, bw_khz=bw_khz,
                   cr_denom=cr_denom, power_by_node=power_by_node,
                   default_dbm=default_dbm, source=source)


def slots_per_frame(node: dict, total_slots: int) -> dict[str, float]:
    """A node's real schedule: slots of each type per superframe.

    `pct_by_type` is a TIME fraction, which is the right quantity for energy
    (current x time). Scaling it by the frame size expresses the same measurement
    in slots, which is what reconciles against the Network Manager's own budget
    (`superframe.extract`): CONTROL_RX + CONTROL_TX ~ control, and so on.

    Slot durations are not perfectly uniform — the firmware truncates an active
    slot to ~5 ms on the stop/start resync path — so these are effective slots,
    not integers. Do not round them; the fractional part is real.
    """
    pct = node.get("pct_by_type") or {}
    return {k: v / 100.0 * total_slots for k, v in pct.items()}


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Compute duty cycle / energy model for one run")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--source", choices=SOURCES, default="best",
                    help="evidence base for the per-state currents: 'best' "
                         "(default; power-keyed, MIXED provenance -- TX/SLEEP "
                         "measured, RX/IDLE datasheet), 'datasheet', or "
                         "'legacy' (the flat 120 mA table, reproduces "
                         "pre-fix numbers exactly)")
    ap.add_argument("--out", type=Path, default=None,
                    help="output path (default: <run_dir>/duty_cycle.json)")
    args = ap.parse_args()
    result = analyse(args.run_dir, source=args.source)
    out = args.out or (args.run_dir / "duty_cycle.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
