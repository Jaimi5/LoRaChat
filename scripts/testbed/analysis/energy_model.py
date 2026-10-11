"""Modelled whole-node energy: radio + MCU state + board floor, per node.

Extends analysis/duty_cycle.py (radio slot integration) with the ESP32 side, so
light and deep sleep show up in the energy figures. Everything here is
MODELLED: per-state currents (analysis/radio_power.py for the radio,
analysis/energy_params.yaml for the MCU and the board) times the time each node
spent in each state according to its logs. Outputs carry "label": "modelled".

Per v2 node, over its slot transitions inside the measurement window:

  radio   every gap between two `Slot N transition` lines is charged at that
          slot type's current (radio_power.current_table at the node's
          lora_power), except the part inside a deep sleep, charged at SLEEP.
  MCU     deep   = announced deep sleeps ("Deep sleep for", ended by rst:0x5)
          boot   = wakes x boot ms (rst:0x5 -> BOOT_READY, node median, else the
                   run median, else mcu.boot_ms_default)
          light  = HEALTH lsl_ms deltas, capped by the radio-SLEEP time left
                   after deep + boot
          active = the rest
  board   the board floor (AXP192 quiescent + GPS + OLED + other) all the time.

Two variants: "chip" (radio + MCU) and "board" (chip + floor). SLEEP-slot time
is reported split into deep / light / awake. v1 nodes (no slots) use the
duty_cycle continuous-RX radio model and an always-active MCU.

Usage:
    python3 scripts/testbed/analysis/energy_model.py <run_dir> [--params yaml] [--out json]
"""

from __future__ import annotations

import json
import statistics
from collections import defaultdict
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

import yaml

from analysis.duty_cycle import _read_lora_config, _read_node_power
from analysis.duty_cycle import compute as duty_compute
from analysis.parse_logs import Event, load_measurement_window, parse_run
from analysis.radio_power import current_table
from analysis.sleep_metrics import deep_sleep_intervals, light_sleep_deltas

PARAMS_PATH = Path(__file__).resolve().parent / "energy_params.yaml"


def load_params(path: Path | None = None) -> dict:
    with open(path or PARAMS_PATH) as f:
        return yaml.safe_load(f)


def board_floor_mA(params: dict) -> float:
    b = params.get("board") or {}
    return (float(b.get("axp192_quiescent_mA", 0.0)) + float(b.get("oled_mA", 0.0))
            + float(b.get("other_mA", 0.0))
            + (float(b.get("gps_mA", 0.0)) if b.get("gps_on", True) else 0.0))


def _overlap(a0: float, a1: float, b0: float, b1: float) -> float:
    return max(0.0, min(a1, b1) - max(a0, b0))


def _node_boot_ms(items: list[dict]) -> float | None:
    xs = [(it["ready"] - it["wake"]) * 1000.0 for it in items
          if it["wake"] is not None and it["ready"] is not None]
    return statistics.median(xs) if xs else None


def compute(events: list[Event], all_events: list[Event], params: dict,
            window: tuple[float | None, float | None] = (None, None),
            power_by_node: dict[str, float] | None = None, default_dbm: float = 17.0,
            sf: int | None = None, bw_khz: float = 125.0, cr_denom: int = 7,
            source: str = "best") -> dict:
    """`events` are the window's events, `all_events` the full stream (deep
    sleeps are paired with their wake across the window edges)."""
    power_by_node = power_by_node or {}
    mcu = params.get("mcu") or {}
    i_act, i_light = float(mcu["active_mA"]), float(mcu["light_mA"])
    i_deep, i_boot = float(mcu["deep_mA"]), float(mcu.get("boot_mA", mcu["active_mA"]))
    floor = board_floor_mA(params)
    volt = float(params.get("voltage_v", 3.3))

    trans: dict[str, list[tuple[float, str]]] = defaultdict(list)
    for e in events:
        if e.kind == "slot":
            trans[e.node].append((e.ts, e.fields["type"].upper()))
    intervals = deep_sleep_intervals(all_events)
    light = light_sleep_deltas(all_events, window)
    run_boot = [b for b in (_node_boot_ms(v) for v in intervals.values()) if b is not None]
    boot_default = (statistics.median(run_boot) if run_boot
                    else float(mcu.get("boot_ms_default", 400)))

    duty = None
    per_node: dict[str, dict] = {}
    for node in sorted(set(trans) | {e.node for e in events if e.kind == "v1_pkt_send"}):
        tr = trans.get(node, [])
        dbm = power_by_node.get(node, default_dbm)
        table = current_table(dbm, source=source)
        if len(tr) < 2:
            # v1 (or a node with no slot data): continuous-RX radio, MCU always on.
            if duty is None:
                win_s = (window[1] - window[0]) if None not in window else None
                duty = duty_compute(events, window_s=win_s, sf=sf, bw_khz=bw_khz,
                                    cr_denom=cr_denom, power_by_node=power_by_node,
                                    default_dbm=default_dbm, source=source)
            d = duty["per_node"].get(node) or {}
            if not d.get("window_ms"):
                continue
            t_ms = d["window_ms"]
            radio_mAs = d["energy_mJ"] / volt
            mcu_ms = {"active": t_ms, "light": 0.0, "deep": 0.0, "boot": 0.0}
            sleep_split = None
            n_wakes = 0
        else:
            t_first, t_last = tr[0][0], tr[-1][0]
            t_ms = (t_last - t_first) * 1000.0
            deep = [(it["start"], it["end"]) for it in intervals.get(node, [])]
            radio_mAs = 0.0
            sleep_ms = deep_in_sleep_ms = deep_ms = 0.0
            for (a, kind), (b, _) in zip(tr, tr[1:]):
                if b <= a:
                    continue
                d_ms = sum(_overlap(a, b, s0, s1) for s0, s1 in deep) * 1000.0
                g_ms = (b - a) * 1000.0
                d_ms = min(d_ms, g_ms)
                deep_ms += d_ms
                radio_mAs += (table.get(kind, 0.0) * (g_ms - d_ms) + table["SLEEP"] * d_ms) / 1000.0
                if kind == "SLEEP":
                    sleep_ms += g_ms
                    deep_in_sleep_ms += d_ms
            wakes = [it for it in intervals.get(node, [])
                     if it["wake"] is not None and t_first <= it["wake"] <= t_last]
            n_wakes = len(wakes)
            boot_each = _node_boot_ms(intervals.get(node, [])) or boot_default
            boot_ms = min(n_wakes * boot_each, max(t_ms - deep_ms, 0.0))
            light_ms = min(float((light.get(node) or {}).get("lsl_ms", 0)),
                           max(sleep_ms - deep_in_sleep_ms - boot_ms, 0.0))
            active_ms = max(t_ms - deep_ms - boot_ms - light_ms, 0.0)
            mcu_ms = {"active": active_ms, "light": light_ms, "deep": deep_ms, "boot": boot_ms}
            sleep_split = {"sleep_slot_ms": sleep_ms, "deep_ms": deep_in_sleep_ms,
                           "light_ms": light_ms,
                           "awake_ms": max(sleep_ms - deep_in_sleep_ms - light_ms, 0.0)}
        mcu_mAs = (i_act * mcu_ms["active"] + i_light * mcu_ms["light"]
                   + i_deep * mcu_ms["deep"] + i_boot * mcu_ms["boot"]) / 1000.0
        chip_mAs = radio_mAs + mcu_mAs
        board_mAs = chip_mAs + floor * t_ms / 1000.0
        t_s = t_ms / 1000.0
        per_node[node] = {
            "window_ms": t_ms,
            "tx_power_dbm": dbm,
            "radio_mean_current_mA": radio_mAs / t_s if t_s else None,
            "mcu_ms": mcu_ms,
            "mcu_mean_current_mA": mcu_mAs / t_s if t_s else None,
            "sleep_slot_split": sleep_split,
            "deep_wakes": n_wakes,
            "chip": {"energy_mJ": chip_mAs * volt,
                     "mean_current_mA": chip_mAs / t_s if t_s else None},
            "board": {"energy_mJ": board_mAs * volt,
                      "mean_current_mA": board_mAs / t_s if t_s else None},
        }

    def mean_of(variant):
        xs = [v[variant]["mean_current_mA"] for v in per_node.values()
              if v[variant]["mean_current_mA"] is not None]
        return statistics.fmean(xs) if xs else None

    return {
        "label": "modelled",
        "voltage_v": volt,
        "radio_source": source,
        "board_floor_mA": floor,
        "boot_ms_used_default": boot_default,
        "mean_current_mA": {"chip": mean_of("chip"), "board": mean_of("board")},
        "mean_mcu_current_mA": (statistics.fmean([v["mcu_mean_current_mA"] for v in per_node.values()
                                                  if v["mcu_mean_current_mA"] is not None])
                                if per_node else None),
        "per_node": per_node,
        "params": params,
    }


def analyse(run_dir: Path, params: dict | None = None, source: str = "best") -> dict:
    run_dir = Path(run_dir)
    params = params or load_params()
    all_events = parse_run(run_dir, window=(None, None))
    w = load_measurement_window(run_dir)
    events = [e for e in all_events if (w[0] is None or e.ts >= w[0]) and (w[1] is None or e.ts <= w[1])]
    sf, bw, cr = _read_lora_config(run_dir)
    power, default_dbm = _read_node_power(run_dir)
    res = compute(events, all_events, params, window=w, power_by_node=power,
                  default_dbm=default_dbm, sf=sf, bw_khz=bw, cr_denom=cr, source=source)
    res["run"] = run_dir.name
    return res


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Modelled radio + MCU + board energy for one run")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--params", type=Path, default=None, help=f"default: {PARAMS_PATH.name}")
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()
    res = analyse(args.run_dir, load_params(args.params))
    text = json.dumps(res, indent=2, sort_keys=True)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text)
        print(f"wrote {args.out}")
    else:
        print(json.dumps({"label": res["label"], "mean_current_mA": res["mean_current_mA"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
