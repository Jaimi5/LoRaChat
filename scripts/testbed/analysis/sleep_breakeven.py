"""When does member deep sleep beat light sleep? Break-even model and check.

The library deep-sleeps a member through a run of SLEEP slots that ends at a
sync beacon, and light-sleeps every other run (lora_mesh_protocol.cpp
SleepThroughSleepRun / TryDeepSleep). For one run of length W (ms, from the
decision to the next active slot), the ESP32 charge is (MODELLED, currents from
energy_params.yaml):

  light  i_L (W - m_L) + i_A m_L          m_L = guard + ppm W
  deep   i_D (W - M) + i_boot b + i_A (M - b)
                                          M = boot budget + guard + ppm W
                                          b = actual boot (<= budget)

The node wakes M early and, after booting for b, waits awake until its slot:
the campaign's deep cells cost what this predicts only with that wait charged
at the active current (see validate()). Deep sleep is taken only when
W - M >= min_sleep_ms. Setting the two equal gives the break-even window

  W* = [(i_A - i_D) M0 - (i_A - i_L) g + (i_boot - i_A) b] / ((i_L - i_D)(1 - ppm))
     ~ i_A * boot_budget / (i_L - i_D)

with M0 = boot budget + guard. The radio sleeps in SLEEP slots either way, so
it cancels out, and so does the board floor (GPS, OLED, PMU).

W comes from the superframe (slot_scheduler.cpp): with A active slots,
  total = max(ceil(nm_tx / duty / slot), ceil(A / (1 - f)), A + 2)
  W ~ (total - A) * slot
where A = 3(H+1) + N + N * data_slots for N members and H hops. The beacon
carries the slot count in one byte, so total <= 255 slots: W can never exceed
about (255 - A) * slot (~145 s at SF7, ~200 s at SF9). Slot lengths and
the manager's TX time per superframe are measured from the campaign logs.

Usage:
    python3 scripts/testbed/analysis/sleep_breakeven.py [--light RUN] [--deep RUN ...]
        [--png out.png] [--json out.json]
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from dataclasses import asdict, dataclass, replace
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.energy_model import load_params

# Measured in the lm200 campaigns (manager logs, 2.0.0 slot formula, 125 kHz).
# slot_ms: spacing of `Slot N transition` lines. nm_tx_ms: the manager's TX time
# per superframe at 13 members, from "Updated slot table: … x% TX duty cycle".
SLOT_MS = {7: 650, 9: 950, 11: 1850, 12: 3300}
NM_TX_MS_13 = {7: 880, 9: 2000, 11: 5800, 12: 11600}   # SF11 interpolated by ToA
NM_TX_SOURCE = {7: "measured", 9: "measured", 11: "estimated", 12: "measured"}


@dataclass
class SleepParams:
    i_active_mA: float = 50.0
    i_light_mA: float = 0.8
    i_deep_mA: float = 0.010
    i_boot_mA: float = 50.0
    boot_budget_ms: float = 4200.0    # LORA_DEEP_SLEEP_BOOT_MS in the campaign
    boot_actual_ms: float = 2125.0    # rst:0x5 -> BOOT_READY, median of the deep cells
    guard_ms: float = 20.0            # wake-up guard (protocol_configuration default)
    drift_ppm: float = 3000.0         # calibrated_drift_ppm
    min_sleep_ms: float = 20000.0     # lora_deep_sleep_min_ms in the campaign

    @classmethod
    def from_energy_params(cls, params: dict, **kw) -> "SleepParams":
        mcu = params.get("mcu") or {}
        return cls(i_active_mA=float(mcu["active_mA"]), i_light_mA=float(mcu["light_mA"]),
                   i_deep_mA=float(mcu["deep_mA"]),
                   i_boot_mA=float(mcu.get("boot_mA", mcu["active_mA"])), **kw)


def margin_deep_ms(w_ms: float, p: SleepParams) -> float:
    return p.boot_budget_ms + p.guard_ms + p.drift_ppm * 1e-6 * w_ms


def deep_allowed(w_ms: float, p: SleepParams) -> bool:
    return w_ms - margin_deep_ms(w_ms, p) >= p.min_sleep_ms


def charge_light_mAs(w_ms: float, p: SleepParams) -> float:
    m = min(w_ms, p.guard_ms + p.drift_ppm * 1e-6 * w_ms)
    return (p.i_light_mA * (w_ms - m) + p.i_active_mA * m) / 1000.0


def charge_deep_mAs(w_ms: float, p: SleepParams) -> float:
    m = min(w_ms, margin_deep_ms(w_ms, p))
    b = min(p.boot_actual_ms, m)
    return (p.i_deep_mA * (w_ms - m) + p.i_boot_mA * b + p.i_active_mA * (m - b)) / 1000.0


def breakeven_ms(p: SleepParams) -> float:
    m0 = p.boot_budget_ms + p.guard_ms
    b = min(p.boot_actual_ms, p.boot_budget_ms)
    num = ((p.i_active_mA - p.i_deep_mA) * m0 - (p.i_active_mA - p.i_light_mA) * p.guard_ms
           + (p.i_boot_mA - p.i_active_mA) * b)
    den = (p.i_light_mA - p.i_deep_mA) * (1 - p.drift_ppm * 1e-6)
    return num / den if den > 0 else math.inf


def window_ms(sf: int, members: int = 13, data_slots: int = 1, hops: int = 1,
              sleep_fraction: float = 0.0, duty: float = 0.1) -> dict:
    """The superframe and its SLEEP run for one configuration (slot_scheduler.cpp)."""
    slot = SLOT_MS[sf]
    active = 3 * (hops + 1) + members + members * data_slots
    nm_tx = NM_TX_MS_13[sf] * members / 13.0
    by_duty = math.ceil(nm_tx / duty / slot)
    by_sleep = math.ceil(active / (1 - sleep_fraction)) if 0 < sleep_fraction < 1 else active
    total = min(max(by_duty, by_sleep, active + 2), 255)
    return {"sf": sf, "slot_ms": slot, "active_slots": active, "total_slots": total,
            "capped_255": max(by_duty, by_sleep, active + 2) > 255,
            "superframe_ms": total * slot, "window_ms": max(total - active, 0) * slot,
            "sized_by": max((by_duty, "duty"), (by_sleep, "sleep_fraction"),
                            (active + 2, "churn_margin"))[1]}


def compare(w_ms: float, superframe_ms: float, p: SleepParams) -> dict:
    """Light vs deep for the SLEEP run, and the ESP32 mean current over the whole
    superframe (the rest of it charged at the active current)."""
    ql, qd = charge_light_mAs(w_ms, p), charge_deep_mAs(w_ms, p)
    allowed = deep_allowed(w_ms, p)
    rest = p.i_active_mA * max(superframe_ms - w_ms, 0.0) / 1000.0
    t = superframe_ms / 1000.0
    light_mA = (rest + ql) / t
    deep_mA = (rest + (qd if allowed else ql)) / t
    return {"window_ms": w_ms, "light_mAs": ql, "deep_mAs": qd, "deep_allowed": allowed,
            "winner": ("deep" if allowed and qd < ql else "light"),
            "esp32_mA_light": light_mA, "esp32_mA_deep": deep_mA,
            "deep_vs_light_pct": 100.0 * (deep_mA - light_mA) / light_mA}


# Configurations worth placing on the curve.
CONFIGS = [
    ("Campaign sleep cells (SF9, f 0.5, duty 10 %)", dict(sf=9, sleep_fraction=0.5, duty=0.1)),
    ("Testbed default (SF9, f 0, duty 10 %)", dict(sf=9, sleep_fraction=0.0, duty=0.1)),
] + [
    (f"Library defaults SF{sf} (f 0.3, duty 1 %, 2 slots)",
     dict(sf=sf, sleep_fraction=0.3, duty=0.01, data_slots=2)) for sf in (7, 9, 11, 12)
] + [
    (f"Low duty SF{sf} (f 0.3, duty 0.1 %, 2 slots)",
     dict(sf=sf, sleep_fraction=0.3, duty=0.001, data_slots=2)) for sf in (7, 9)
]


def _members(cfg_path: Path) -> set[str] | None:
    try:
        import yaml
        cfg = yaml.safe_load(cfg_path.read_text())
    except Exception:
        return None
    mgr = str((cfg.get("defaults") or {}).get("lora_manager_id", "")).replace("0x", "").upper()
    devs = cfg.get("devices") or {}
    return {str(k).upper() for k, v in devs.items()
            if (v or {}).get("node_active", 1) and str(k).upper() != mgr}


def _run_summary(run: Path) -> dict:
    from analysis.energy_model import analyse
    from analysis.parse_logs import parse_run
    res = analyse(run)
    members = _members(run / "config.yaml")
    pn = {k: v for k, v in res["per_node"].items() if members is None or k.upper() in members}
    win = [v["window_ms"] for v in pn.values()]
    mcu = statistics.fmean(v["mcu_mean_current_mA"] for v in pn.values())
    wakes_per_h = statistics.fmean(v["deep_wakes"] / v["window_ms"] * 3.6e6 for v in pn.values())
    runs_w = [e.fields["sleep_ms"] for e in parse_run(run, window=(None, None))
              if e.kind == "deep_sleep" and (members is None or e.node.upper() in members)]
    return {"run": run.name, "members": len(pn), "window_s": statistics.fmean(win) / 1000,
            "mcu_mA": mcu, "deep_wakes_per_h": wakes_per_h,
            "deep_sleep_ms_median": statistics.median(runs_w) if runs_w else None}


def validate(light_run: Path, deep_runs: list[Path], p: SleepParams) -> dict:
    """Predict the deep cells' ESP32 current from the light cell plus the model's
    extra charge per deep run, and compare with the energy model on the logs."""
    light = _run_summary(light_run)
    out = {"light": light, "deep": []}
    for run in deep_runs:
        d = _run_summary(run)
        w = (d["deep_sleep_ms_median"] or 0) + margin_deep_ms(d["deep_sleep_ms_median"] or 0, p)
        extra = charge_deep_mAs(w, p) - charge_light_mAs(w, p)
        d["window_ms_median"] = w
        d["predicted_mcu_mA"] = light["mcu_mA"] + d["deep_wakes_per_h"] * extra / 3600.0
        d["error_pct"] = 100.0 * (d["predicted_mcu_mA"] - d["mcu_mA"]) / d["mcu_mA"]
        out["deep"].append(d)
    return out


def plot(p: SleepParams, path: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    ws = [10 ** (1 + i * 2.3 / 400) * 1000 for i in range(401)]      # 10 s .. 2000 s
    fig, (a, b) = plt.subplots(1, 2, figsize=(12, 4.6))
    a.plot([w / 1000 for w in ws], [charge_light_mAs(w, p) / w * 1000 for w in ws], label="light sleep")
    dw = [w for w in ws if deep_allowed(w, p)]
    a.plot([w / 1000 for w in dw], [charge_deep_mAs(w, p) / w * 1000 for w in dw], label="deep sleep")
    for bb, ls in ((p.boot_budget_ms, "-"), (1200.0, ":")):
        q = replace(p, boot_budget_ms=bb, boot_actual_ms=min(p.boot_actual_ms, bb))
        a.axvline(breakeven_ms(q) / 1000, color="grey", ls=ls, lw=1)
        a.text(breakeven_ms(q) / 1000, 0.97, f" break-even\n boot {bb / 1000:.1f} s", fontsize=8,
               va="top", transform=a.get_xaxis_transform())
    for name, kw in CONFIGS:
        w = window_ms(**kw)["window_ms"]
        if w > 0:
            a.plot(w / 1000, charge_light_mAs(w, p) / w * 1000, "k|", ms=14)
    a.set(xscale="log", yscale="log", xlabel="SLEEP run W (s)", ylabel="mean ESP32 current over the run (mA)",
          title="ESP32 current during one SLEEP run (modelled)")
    a.legend(loc="lower left")
    boots = [200 + 50 * i for i in range(117)]
    for il in (0.8, 1.5, 3.0):
        b.plot([x / 1000 for x in boots],
               [breakeven_ms(replace(p, boot_budget_ms=x, boot_actual_ms=min(x, p.boot_actual_ms),
                                     i_light_mA=il)) / 1000 for x in boots], label=f"light sleep {il} mA")
    b.axvline(p.boot_budget_ms / 1000, color="grey", lw=1)
    b.set(yscale="log", xlabel="boot budget (s)", ylabel="break-even W* (s)",
          title="Deep sleep wins above the line")
    b.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=130)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--light", type=Path)
    ap.add_argument("--deep", type=Path, nargs="*", default=[])
    ap.add_argument("--png", type=Path)
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    p = SleepParams.from_energy_params(load_params())
    out = {"label": "modelled", "params": asdict(p), "breakeven_ms": breakeven_ms(p),
           "breakeven_ms_boot_1200": breakeven_ms(replace(p, boot_budget_ms=1200, boot_actual_ms=1200)),
           "configs": []}
    print(f"break-even W* = {out['breakeven_ms'] / 1000:.0f} s "
          f"(boot budget {p.boot_budget_ms / 1000:.1f} s); "
          f"{out['breakeven_ms_boot_1200'] / 1000:.0f} s with a 1.2 s boot")
    for name, kw in CONFIGS:
        sfm = window_ms(**kw)
        c = compare(sfm["window_ms"], sfm["superframe_ms"], p)
        out["configs"].append({"name": name, **kw, **sfm, **c})
        print(f"  {name:<48} superframe {sfm['superframe_ms'] / 1000:6.1f} s"
              + (" (255-slot cap)" if sfm["capped_255"] else "")
              + f"  W {sfm['window_ms'] / 1000:6.1f} s  ESP32 light {c['esp32_mA_light']:5.1f} mA,"
              f" deep {c['esp32_mA_deep']:5.1f} mA ({c['deep_vs_light_pct']:+.0f} %) -> {c['winner']}"
              + ("" if c["deep_allowed"] else "  [deep not allowed]"))
    if args.light and args.deep:
        out["validation"] = validate(args.light, args.deep, p)
        v = out["validation"]
        print(f"  light cell ESP32 {v['light']['mcu_mA']:.2f} mA")
        for d in v["deep"]:
            print(f"  {d['run']}: measured {d['mcu_mA']:.2f} mA, predicted {d['predicted_mcu_mA']:.2f} mA "
                  f"({d['error_pct']:+.1f} %), {d['deep_wakes_per_h']:.1f} wakes/h, "
                  f"W median {d['window_ms_median'] / 1000:.1f} s")
    if args.png:
        plot(p, args.png)
        print(f"wrote {args.png}")
    if args.json:
        args.json.write_text(json.dumps(out, indent=2, default=str))
        print(f"wrote {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
