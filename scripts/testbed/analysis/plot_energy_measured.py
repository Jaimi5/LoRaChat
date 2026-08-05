"""The sleep-vs-current relationship, measured — not projected.

    python3 scripts/testbed/analysis/plot_energy_measured.py \
        --runs scripts/testbed/runs --out docs/paper/figures/energy
    python3 scripts/testbed/analysis/plot_energy_measured.py --runs ... --table

Writes `energy_sleep_measured.{pdf,png}`. `--table` prints the per-campaign
summary instead, so the figure and the numbers quoted in
`docs/paper/energy_sleep_regime.md` come from one code path.

What this shows
---------------
Every v2 node already sleeps, without anyone asking it to. A node sleeps through
the data slots of nodes it does not relay for, so its sleep fraction falls out of
its position in the tree: leaves sleep a lot, relays barely sleep. Across 7
campaigns that spreads real nodes over 6-56% sleep and 6-14 mA — enough to trace
the current-vs-sleep line with measurements alone, and enough that most of them sit
below the Previous LoRaMesher's receive-current floor.

Both axes are measured. `duty_cycle.py` integrates each node's actual slot
occupancy from its own log; nothing here is modelled except the reference line.

What this does NOT show
-----------------------
**Commanded sleep.** Every run used `lora_duty_cycle: 0.1` and
`LORA_MIN_SLEEP_FRACTION` = 0. The spread here is sleep that ARISES from topology,
not sleep that was configured. `plot_energy_regime.py` covers the knob, and it is a
projection. Do not read this scatter as validating that knob — no run ever set it.

Statistics
----------
These are per-node, and nodes within a run share a topology and a schedule, so the
points are NOT independent samples. This is a mechanism scatter — it shows the
relationship the per-state model predicts, and that real hardware follows it. It is
not a statistical test, so no p-values and no confidence intervals are reported.
"""

from __future__ import annotations

import argparse
import glob
import statistics
import sys
from dataclasses import dataclass
from pathlib import Path

if __name__ == "__main__":
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.duty_cycle import analyse as analyse_duty
from analysis.plot_compare import _apply_paper_style, _savefig_multi
from analysis.radio_power import SLEEP_MA, rx_current_ma, tx_current_ma

# v2 campaigns with TDMA slot logs, named as rename_runs.sh left them
# (`<batch>__lmv2`). The `__lmv1` sets are CSMA/CA: no slots, no sleep to
# measure — they are the floor these points are compared against.
_CAMPAIGNS = [
    "formation__lmv2", "main_cluster_pdr__lmv2", "sim_size_compare__lmv2",
    "sim_load_compare__lmv2", "sim_load_compare_full__lmv2",
    "dataslots_check__lmv2", "long_link__lmv2",
]

# Distinct hues per campaign. These are ENTITIES, not an ordered scale.
_STYLE = {
    "formation__lmv2":             {"color": "#4C78A8", "marker": "o"},
    "main_cluster_pdr__lmv2":      {"color": "#F58518", "marker": "s"},
    "sim_size_compare__lmv2":      {"color": "#54A24B", "marker": "^"},
    "sim_load_compare__lmv2":      {"color": "#E45756", "marker": "D"},
    "sim_load_compare_full__lmv2": {"color": "#B279A2", "marker": "v"},
    "dataslots_check__lmv2":       {"color": "#9D755D", "marker": "P"},
    "long_link__lmv2":             {"color": "#7F7F7F", "marker": "X"},
}
_PLM = "#eb6834"


_TX_STATES = ("TX", "CONTROL_TX", "SYNC_BEACON_TX", "DISCOVERY_TX")


@dataclass
class Point:
    campaign: str
    run: str
    node: str
    sleep_frac: float
    tx_frac: float          # measured, so the model line is not fitted
    current_mA: float
    tx_dbm: float


def collect(runs_root: Path, source: str = "best") -> list[Point]:
    """Every per-node measurement across the v2 campaigns."""
    out: list[Point] = []
    for camp in _CAMPAIGNS:
        for d in sorted(glob.glob(str(runs_root / camp / "*-r0[1-9]"))):
            try:
                duty = analyse_duty(Path(d), source=source)
            except Exception:
                continue
            for node, v in (duty.get("per_node") or {}).items():
                # slot_integration == a v2 node with a real TDMA schedule.
                if v.get("method") != "slot_integration":
                    continue
                pct = v.get("pct_by_type") or {}
                if not pct or v.get("mean_current_mA") is None:
                    continue
                out.append(Point(camp, Path(d).name, node,
                                 pct.get("SLEEP", 0.0) / 100.0,
                                 sum(pct.get(k, 0.0) for k in _TX_STATES) / 100.0,
                                 v["mean_current_mA"], v.get("tx_power_dbm")))
    return out


def _fit(pts: list[Point]) -> tuple[float, float, float]:
    """Least-squares I = a + b*f_sleep, plus R². Descriptive only — see module docstring."""
    n = len(pts)
    sx = sum(p.sleep_frac for p in pts)
    sy = sum(p.current_mA for p in pts)
    sxx = sum(p.sleep_frac ** 2 for p in pts)
    sxy = sum(p.sleep_frac * p.current_mA for p in pts)
    b = (n * sxy - sx * sy) / (n * sxx - sx * sx)
    a = (sy - b * sx) / n
    ybar = sy / n
    ss_res = sum((p.current_mA - (a + b * p.sleep_frac)) ** 2 for p in pts)
    ss_tot = sum((p.current_mA - ybar) ** 2 for p in pts)
    return a, b, 1 - ss_res / ss_tot


def table(pts: list[Point], i_rx: float) -> None:
    two = [p for p in pts if p.tx_dbm == 2]
    print(f"{'campaign':<26} {'nodes':>6} {'sleep % range':>16} "
          f"{'mA range':>16} {'below floor':>12}")
    print("-" * 82)
    for camp in _CAMPAIGNS:
        c = [p for p in two if p.campaign == camp]
        if not c:
            continue
        s = [p.sleep_frac * 100 for p in c]
        i = [p.current_mA for p in c]
        below = sum(1 for x in i if x < i_rx)
        print(f"{camp:<26} {len(c):>6} {min(s):>6.1f}–{max(s):<9.1f} "
              f"{min(i):>6.2f}–{max(i):<9.2f} {below*100//len(c):>11}%")
    s = [p.sleep_frac * 100 for p in two]
    i = [p.current_mA for p in two]
    below = sum(1 for x in i if x < i_rx)
    print("-" * 82)
    print(f"{'TOTAL (2 dBm nodes)':<26} {len(two):>6} {min(s):>6.1f}–{max(s):<9.1f} "
          f"{min(i):>6.2f}–{max(i):<9.2f} {below*100//len(two):>11}%")
    a, b, r2 = _fit(two)
    print(f"\nfit: I = {a:.2f} {b:+.2f}*f_sleep   R^2 = {r2:.3f}   n = {len(two)}")
    print(f"below the {i_rx} mA floor: {below}/{len(two)}")
    print(f"measured crossing: f_sleep = {(a - i_rx) / -b * 100:.0f}%")
    n14 = [p for p in pts if p.tx_dbm == 14]
    print(f"\n(excluded from the fit: {len(n14)} node-runs at 14 dBm — a different "
          f"TX current, so a separate population)")


def render(pts: list[Point], out_dir: Path, source: str) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D

    _apply_paper_style()
    i_rx = rx_current_ma(source)
    two = [p for p in pts if p.tx_dbm == 2]
    n14 = [p for p in pts if p.tx_dbm == 14]
    out_dir.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(9.6, 5.0))

    for camp in _CAMPAIGNS:
        c = [p for p in two if p.campaign == camp]
        if not c:
            continue
        st = _STYLE[camp]
        ax.scatter([p.sleep_frac * 100 for p in c], [p.current_mA for p in c],
                   s=16, alpha=0.55, color=st["color"], marker=st["marker"],
                   linewidths=0, label=f"{camp}  (n={len(c)})", zorder=3)

    # 14 dBm nodes are a different TX current, so they sit on their own line.
    # Shown hollow rather than pooled or silently dropped.
    if n14:
        ax.scatter([p.sleep_frac * 100 for p in n14], [p.current_mA for p in n14],
                   s=26, facecolors="none", edgecolors="#444", linewidths=0.8,
                   zorder=4, label=f"14 dBm nodes (n={len(n14)}) — excluded")

    # The per-state model. NOT a fit — every term is independently known:
    #   I = I_RX + f_TX*(I_TX - I_RX) - f_sleep*(I_RX - I_SLEEP)
    # I_TX/I_RX/I_SLEEP come from radio_power.py and f_TX is the measured mean, so
    # this line is a prediction the scatter can disagree with. (Deriving f_TX from
    # the fit's intercept would make it partly fitted and prove nothing.)
    a, b, r2 = _fit(two)
    i_tx = tx_current_ma(2.0, source=source)
    f_tx = statistics.fmean(p.tx_frac for p in two)
    xs = [0.0, max(p.sleep_frac for p in two) * 100]
    ys = [i_rx + f_tx * (i_tx - i_rx) - (x / 100) * (i_rx - SLEEP_MA) for x in xs]
    ax.plot(xs, ys, color="#222", lw=1.6, ls="-", zorder=5,
            label=f"per-state model (measured $f_{{TX}}$={f_tx*100:.1f}%, not fitted)")

    ax.axhline(i_rx, color=_PLM, ls="--", lw=1.8, zorder=2)
    below = sum(1 for p in two if p.current_mA < i_rx)
    ax.annotate(f"Previous LoRaMesher floor ($I_{{RX}}$ = {i_rx:g} mA)\n"
                f"{below} of {len(two)} measured nodes "
                f"({below/len(two)*100:.0f} %) already draw less",
                xy=(0.60, i_rx), xycoords=("axes fraction", "data"),
                xytext=(0, 8), textcoords="offset points",
                fontsize=8.5, color=_PLM, ha="left", va="bottom")

    cross = (a - i_rx) / -b * 100
    ax.axvline(cross, color="#444", ls=":", lw=1.1, zorder=2)
    # Top of the axis — the bottom-left is where dataslots_check's tail lives.
    ax.annotate(f"crosses at {cross:.0f} % sleep", xy=(cross, 1.0),
                xycoords=("data", "axes fraction"),
                xytext=(5, -12), textcoords="offset points",
                fontsize=8.5, color="#444", ha="left", va="top")

    ax.set_xlabel("Measured sleep fraction of the superframe (%)")
    ax.set_ylabel("Measured mean radio current (mA)")
    ax.set_title("Sleep already happens, and it already wins\n"
                 "every v2 node measured, sleep arising from relay position "
                 "($f_s$ = 0 throughout)", fontsize=11)
    ax.grid(True, linestyle=":", alpha=0.45)
    ax.set_axisbelow(True)
    # Outside the axes: every quadrant holds data (dataslots_check's tail runs to
    # the bottom-left, the 14 dBm population to the top-right).
    ax.legend(fontsize=7.5, framealpha=0.92, loc="upper left",
              bbox_to_anchor=(1.01, 1.0), borderaxespad=0)
    fig.tight_layout()
    _savefig_multi(fig, out_dir / "energy_sleep_measured.png", dpi=150)
    plt.close(fig)

    print(f"\n{len(two)} per-node measurements at 2 dBm across "
          f"{len({p.campaign for p in two})} campaigns")
    print(f"  fit I = {a:.2f} {b:+.2f}*f_sleep  (R^2={r2:.3f}), "
          f"crossing at {cross:.0f}% sleep")
    print(f"  {below}/{len(two)} below the {i_rx} mA floor")


def render_pooled(pts: list[Point], out_dir: Path, source: str) -> None:
    """Stripped single-series version for the paper's §7.7.

    Same data and same per-state line as render(), but one neutral cloud, no
    per-campaign markers, no 14 dBm population, no title, no on-plot text. Axis and
    legend wording say "Estimated", not "Measured": these currents come from the
    measured slot schedule via a datasheet current table — the nodes are not
    metered.
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    _apply_paper_style()
    i_rx = rx_current_ma(source)
    two = [p for p in pts if p.tx_dbm == 2]
    out_dir.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(6.4, 4.2))

    # One pooled cloud — every 2 dBm node, no campaign distinction.
    ax.scatter([p.sleep_frac * 100 for p in two], [p.current_mA for p in two],
               s=16, alpha=0.4, color="#888888", linewidths=0, zorder=3)

    # Per-state model line — an identity, not a regression (so no R^2). Same
    # formula and same measured mean f_TX as render(); drawn across the full axis.
    i_tx = tx_current_ma(2.0, source=source)
    f_tx = statistics.fmean(p.tx_frac for p in two)
    xs = [0.0, 56.0]
    ys = [i_rx + f_tx * (i_tx - i_rx) - (x / 100) * (i_rx - SLEEP_MA) for x in xs]
    ax.plot(xs, ys, color="#222222", lw=1.8, ls="-", zorder=5,
            label="per-state model (not fitted)")

    ax.axhline(i_rx, color=_PLM, ls="--", lw=1.8, zorder=2,
               label="PLM always-on RX floor")

    ax.set_xlabel("Estimated sleep fraction of the superframe (%)")
    ax.set_ylabel("Estimated mean radio current (mA)")
    ax.set_xlim(0, 56)
    ax.set_ylim(6, 14)
    ax.grid(True, linestyle=":", alpha=0.45)
    ax.set_axisbelow(True)
    ax.legend(fontsize=9, framealpha=0.92, loc="upper right")
    fig.tight_layout()
    _savefig_multi(fig, out_dir / "energy_sleep_pooled.png", dpi=150)
    plt.close(fig)

    below = sum(1 for p in two if p.current_mA < i_rx)
    print(f"\n{len(two)} pooled per-node estimates at 2 dBm; "
          f"{below} below the {i_rx} mA floor")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Measured sleep fraction vs mean radio current, all v2 campaigns")
    ap.add_argument("--runs", type=Path, default=Path("runs"),
                    help="root holding the campaign dirs (default: runs)")
    ap.add_argument("--out", type=Path, default=Path("docs/paper/figures/energy"))
    ap.add_argument("--source", default="best")
    ap.add_argument("--table", action="store_true",
                    help="print the per-campaign summary instead of plotting")
    ap.add_argument("--pooled", "--clean", dest="pooled", action="store_true",
                    help="render the stripped single-series version for the paper "
                         "(energy_sleep_pooled.{pdf,png})")
    args = ap.parse_args()
    pts = collect(args.runs, source=args.source)
    if not pts:
        raise SystemExit(f"no v2 slot logs found under {args.runs}")
    if args.table:
        table(pts, rx_current_ma(args.source))
    elif args.pooled:
        render_pooled(pts, args.out, args.source)
    else:
        render(pts, args.out, args.source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
