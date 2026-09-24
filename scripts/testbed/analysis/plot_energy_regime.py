"""Where scheduled sleep beats an always-on receiver — the energy regime figure.

    python3 scripts/testbed/analysis/plot_energy_regime.py \
        --v1 scripts/testbed/runs/sim_load_compare_prev \
        --v2 scripts/testbed/runs/sim_load_compare_v2 \
        --out docs/paper/figures/energy

Writes `energy_sleep_regime.{pdf,png}`.

Why this axis pair
------------------
x = superframe RECURRENCE, y = mean radio current. Not "current vs sleep
fraction": mean current is *invariant* in SF at fixed `min_sleep_fraction`
(msf fixes slot composition; SF fixes only slot duration), so a per-SF sweep over
msf would draw three exactly overlapping curves. Plotting against recurrence puts
that invariance to work — the curves share a y-axis and separate on x, which
*demonstrates* the invariance instead of hiding it.

It also lets PLM be drawn honestly. PLM is always receiving or transmitting, and
transmitting costs more, so its mean current is >= the receive current whatever
its load, topology or node count (apart from sub-percent driver turnaround: the
driver returns to standby between TX and startReceive()). That is a horizontal
BOUND, not a trajectory — PLM has no coordinate on a recurrence axis, because its
latency (airtime + backoff, ~1 s) is a different quantity from NLM's scheduled
revisit. Hence a line and a hatch, never a point.

The floor is a wire field, not the radio
----------------------------------------
Mean current is `fixed_active_cost / total_slots + I_SLEEP` — a constant cost
amortised over the frame — so the true asymptote is the sleep current. What stops
NLM at ~1.9 mA is `uint8_t total_slots` in the sync beacon: the frame cannot
exceed 255 slots. The dashed continuation shows what a uint16_t field would
allow. That is a design-space projection the parameter-free model supports, NOT a
proposal and NOT shipped code: widening the field is a wire-format break that
needs a protocol-version bump.

Everything here is model output except the deployed marker, which is measured on
both axes (recurrence from slot-index wraps, current from slot integration).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

if __name__ == "__main__":
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.model import MAX_SLOTS, MAX_SLOTS_UINT16, metrics as model_metrics
from analysis.multirun import _SF_OF_CELL
from analysis.plot_compare import _apply_paper_style, _savefig_multi
from analysis.plot_load import _SF_STYLE
from analysis.radio_power import SLEEP_MA, SOURCES, rx_current_ma
from analysis.superframe import extract_slot_wrap

# Sweep knots. Dense near the crossing (~0.13) and near the cap, where the curve
# turns; sparse in between, where it is smooth.
_MSF_KNOTS = [0.0, 0.05, 0.10, 0.13, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70,
              0.80, 0.85, 0.90, 0.95, 0.98, 0.99, 0.995, 0.999]

# Sleep fractions real nodes already reach from relay position alone, measured
# across 646 per-node samples in 7 campaigns (plot_energy_measured.py --table).
# Shown here only to locate this figure's axis against that one — this figure's
# curves are the KNOB, which no run ever set.
_MEASURED_SLEEP_LO, _MEASURED_SLEEP_HI = 0.061, 0.560

_PLM = "#eb6834"        # matches plot_compare._V1
_HATCH = "#eb6834"


def _plm_floor_by_sf(v1_dir: Path) -> dict[int, float]:
    """Measured PLM mean current per SF, from the aggregated campaign."""
    agg = v1_dir / "aggregated.json"
    if not agg.is_file():
        raise SystemExit(f"missing {agg} — run multirun.py on {v1_dir} first")
    by_sf: dict[int, list[float]] = {}
    for cell, c in (json.loads(agg.read_text()).get("cells") or {}).items():
        m = _SF_OF_CELL.search(cell)
        val = (c.get("mean_current_mA") or {}).get("mean")
        if m and val is not None:
            by_sf.setdefault(int(m.group(1)), []).append(val)
    return {sf: sum(v) / len(v) for sf, v in by_sf.items()}


def _deployed_point(v2_dir: Path, sf: int, source: str) -> dict | None:
    """The measured operating point for `sf`: (recurrence_s, mean_current_mA).

    Both axes measured, no model: recurrence from the slot-index wrap in the
    device logs, current from the slot-integration energy model over the same
    logs. Returns None if either is unavailable.
    """
    runs = sorted(p for p in v2_dir.iterdir()
                  if p.is_dir() and (m := _SF_OF_CELL.search(p.name))
                  and int(m.group(1)) == sf)
    for run in runs:
        wrap = extract_slot_wrap(run)
        if not wrap:
            continue
        from analysis.duty_cycle import analyse as analyse_duty
        try:
            duty = analyse_duty(run, source=source)
        except Exception:
            continue
        currents = [v.get("mean_current_mA")
                    for v in (duty.get("per_node") or {}).values()
                    if isinstance(v, dict) and v.get("mean_current_mA") is not None]
        if not currents:
            continue
        from analysis.model import slot_duration_ms
        slot_ms = slot_duration_ms(sf)
        return {
            "run": run.name,
            "total_slots": wrap["total_slots"],
            "n_nodes": wrap["n_nodes"],
            "confidence": wrap["confidence"],
            "recurrence_s": wrap["total_slots"] * slot_ms / 1000.0,
            "mean_current_mA": sum(currents) / len(currents),
        }
    return None


def _curve(sf: int, nodes: int, max_hops: int, tx_dbm: float, source: str,
           max_slots: int) -> tuple[list[float], list[float], list[float]]:
    """(recurrence, current, msf) swept over the sleep knots."""
    xs, ys, ks = [], [], []
    seen: set[int] = set()
    for msf in _MSF_KNOTS:
        m = model_metrics(sf, node_count=nodes, data_slots=1, duty_cycle=0.10,
                          max_hops=max_hops, tx_power_dbm=tx_dbm, source=source,
                          min_sleep_fraction=msf, max_slots=max_slots)
        # Past the cap every knot returns the same frame; keep one so the solid
        # curve ends at the cap rather than piling points on top of each other.
        if m["total_slots"] in seen:
            continue
        seen.add(m["total_slots"])
        xs.append(m["recurrence_s"])
        ys.append(m["mean_current_mA"])
        ks.append(msf)
    return xs, ys, ks


def render(v1_dir: Path, v2_dir: Path, out_dir: Path, nodes: int, max_hops: int,
           tx_dbm: float, source: str, marker_sf: int, max_slots: int) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch

    _apply_paper_style()
    floors = _plm_floor_by_sf(v1_dir)
    i_rx = rx_current_ma(source)
    out_dir.mkdir(parents=True, exist_ok=True)

    fig, ax = plt.subplots(figsize=(7.8, 5.0))
    sfs = sorted(floors) or [7, 9, 12]

    for sf in sfs:
        st = _SF_STYLE.get(sf, {"color": "#888", "label": f"SF{sf}"})
        # Solid: reachable with today's uint8 field. Dashed: what a uint16 field
        # would allow — same model, one byte wider.
        x8, y8, _ = _curve(sf, nodes, max_hops, tx_dbm, source, MAX_SLOTS)
        x16, y16, _ = _curve(sf, nodes, max_hops, tx_dbm, source, max_slots)
        ax.plot(x8, y8, color=st["color"], lw=2.2, zorder=3,
                label=f"T-LoRaMesher {st['label']} (bound)")
        if max_slots > MAX_SLOTS:
            # Start the dashed run AT the last solid point, so the cap reads as a
            # step in the same curve rather than two disconnected curves.
            tail = [(x, y) for x, y in zip(x16, y16) if x > x8[-1]]
            tail_x = [x8[-1]] + [x for x, _ in tail]
            tail_y = [y8[-1]] + [y for _, y in tail]
            ax.plot(tail_x, tail_y, color=st["color"], lw=1.6, ls=(0, (4, 3)),
                    alpha=0.85, zorder=3)

    # PLM: a BOUND, not a trajectory. Shade the region ABOVE it — where NLM
    # loses. Hatching "below" would be useless here: on a log axis the winning
    # region is ~the whole panel, so the losing sliver is the informative one.
    ax.axhline(i_rx, color=_PLM, ls="--", lw=1.6, zorder=2)
    # The sleep current: what the dashed curves tend to, and the reason the
    # ~1.9 mA step is a wire field rather than a floor.
    ax.axhline(SLEEP_MA, color="#555", ls=":", lw=1.2, zorder=2)

    ax.set_xscale("log")
    ax.set_yscale("log")
    # Headroom above the floor for the losing band + its label, and below for the
    # sleep asymptote. Set before the span so it does not drag the limits.
    ax.set_ylim(SLEEP_MA * 0.55, i_rx * 2.4)
    ax.axhspan(i_rx, ax.get_ylim()[1], facecolor=_HATCH, alpha=0.10,
               lw=0, zorder=0)
    ax.annotate("Previous LoRaMesher cannot go below its receive current,\n"
                "whatever its load, topology or node count",
                xy=(0.985, i_rx), xycoords=("axes fraction", "data"),
                xytext=(0, 6), textcoords="offset points",
                fontsize=8.5, color=_PLM, ha="right", va="bottom")

    # The sleep fractions real nodes already reach, without the knob — measured
    # across 7 campaigns (see plot_energy_measured.py). Mapped onto this axis via
    # the frame length each would need, so the two figures visibly meet.
    lo_x = hi_x = None
    for msf, ref in ((_MEASURED_SLEEP_LO, "lo"), (_MEASURED_SLEEP_HI, "hi")):
        m = model_metrics(marker_sf, node_count=nodes, data_slots=1,
                          duty_cycle=0.10, max_hops=max_hops,
                          tx_power_dbm=tx_dbm, source=source,
                          min_sleep_fraction=msf, max_slots=max_slots)
        if ref == "lo":
            lo_x = m["recurrence_s"]
        else:
            hi_x = m["recurrence_s"]
    if lo_x and hi_x and hi_x > lo_x:
        ax.axvspan(lo_x, hi_x, color="#54A24B", alpha=0.10, lw=0, zorder=0)
        # Rotated inside the band, kept short: the top-left holds the deployed
        # marker's label and the bottom-left the legend, so there is no free
        # horizontal room. The caption carries the full explanation.
        ax.text((lo_x * hi_x) ** 0.5, 0.60,
                f"measured sleep, no knob "
                f"({_MEASURED_SLEEP_LO*100:.0f}–{_MEASURED_SLEEP_HI*100:.0f} %)",
                transform=ax.get_xaxis_transform(), rotation=90,
                fontsize=7.5, color="#3d7a35", ha="center", va="center")

    dep = _deployed_point(v2_dir, marker_sf, source)
    if dep:
        ax.scatter([dep["recurrence_s"]], [dep["mean_current_mA"]], s=90,
                   color=_SF_STYLE.get(marker_sf, {}).get("color", "#333"),
                   edgecolor="black", linewidth=1.1, zorder=6)
        ax.annotate(
            f"as deployed ($f_s$=0)\n{dep['total_slots']} slots, "
            f"{dep['recurrence_s']:.0f} s, {dep['mean_current_mA']:.1f} mA\n"
            f"(measured, both axes)",
            xy=(dep["recurrence_s"], dep["mean_current_mA"]),
            xytext=(26, -6), textcoords="offset points", fontsize=8.5,
            ha="left", va="top",
            arrowprops=dict(arrowstyle="-", lw=0.8, color="#444",
                            shrinkA=0, shrinkB=7))

    # Anchor the cap label to where SF9's solid curve actually ends.
    x9, y9, _ = _curve(marker_sf, nodes, max_hops, tx_dbm, source, MAX_SLOTS)
    ax.annotate("255-slot cap: a $\\mathtt{uint8}$ wire field, not a floor\n"
                "dashed = same model, $\\mathtt{uint16}$ $\\mathtt{total\\_slots}$",
                xy=(x9[-1], y9[-1]), xytext=(16, -26), textcoords="offset points",
                fontsize=8.5, color="#333", ha="left", va="top",
                arrowprops=dict(arrowstyle="->", lw=0.9, color="#444",
                                shrinkA=0, shrinkB=4))
    ax.annotate(f"sleep current {SLEEP_MA:g} mA — the true asymptote",
                xy=(0.985, SLEEP_MA), xycoords=("axes fraction", "data"),
                xytext=(0, 6), textcoords="offset points",
                fontsize=8, color="#555", ha="right", va="bottom")
    ax.set_xlabel("T-LoRaMesher superframe recurrence (s, log) — "
                  "the cost of sleeping")
    ax.set_ylabel("Mean radio current (mA, log)")
    # Says plainly what this figure is, and what it is not. The companion
    # energy_sleep_measured.png carries the measured sleep; this one is the knob.
    ax.set_title("What commanding sleep would buy\n"
                 "model projection — no run set $f_s$ > 0; curves are a worst-case "
                 "upper bound", fontsize=11)
    ax.grid(True, which="both", linestyle=":", alpha=0.45)
    ax.set_axisbelow(True)

    handles, _ = ax.get_legend_handles_labels()
    handles += [
        Line2D([], [], color=_PLM, ls="--", lw=1.6,
               label=f"Previous LoRaMesher bound ($I_{{RX}}$ = {i_rx:g} mA)"),
        Patch(facecolor=_HATCH, alpha=0.10,
              label="T-LoRaMesher loses (above the bound)"),
    ]
    # Narrow (single column) and bottom-left, clear of the sleep-current label
    # that runs along the bottom right.
    ax.legend(handles=handles, fontsize=8, framealpha=0.92, loc="lower left")
    fig.tight_layout()
    _savefig_multi(fig, out_dir / "energy_sleep_regime.png", dpi=150)
    plt.close(fig)

    if dep:
        print(f"\ndeployed marker (measured): {dep['run']}")
        print(f"  {dep['total_slots']} slots x {dep['n_nodes']} nodes, "
              f"wrap confidence {dep['confidence']*100:.0f}%")
        print(f"  recurrence {dep['recurrence_s']:.1f} s, "
              f"current {dep['mean_current_mA']:.2f} mA")
        mdl = model_metrics(marker_sf, node_count=nodes, data_slots=1,
                            duty_cycle=0.10, max_hops=max_hops,
                            tx_power_dbm=tx_dbm, source=source)
        print(f"  model at the same point: {mdl['total_slots']} slots, "
              f"{mdl['mean_current_mA']:.2f} mA "
              f"({abs(mdl['mean_current_mA']-dep['mean_current_mA'])/dep['mean_current_mA']*100:.1f}% off)")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Energy regime figure: mean current vs superframe recurrence")
    ap.add_argument("--v1", type=Path, required=True,
                    help="PLM campaign dir (for the measured floor)")
    ap.add_argument("--v2", type=Path, required=True,
                    help="NLM campaign dir (for the measured deployed marker)")
    ap.add_argument("--out", type=Path, default=Path("docs/paper/figures/energy"))
    ap.add_argument("--nodes", type=int, default=13,
                    help="node count for the model curves (default: the testbed's 13)")
    ap.add_argument("--max-hops", type=int, default=2,
                    help="network depth. NOT a config knob — the firmware derives "
                         "it from sync beacons. 2 is FITTED to the measured "
                         "37-slot frame; state that wherever it is quoted.")
    ap.add_argument("--tx-power", type=float, default=2.0, metavar="DBM",
                    help="TX power (default 2 dBm, the dense-cluster setting)")
    ap.add_argument("--source", choices=SOURCES, default="best")
    ap.add_argument("--marker-sf", type=int, default=9,
                    help="SF to carry the deployed marker (default 9)")
    ap.add_argument("--max-slots", type=int, default=MAX_SLOTS_UINT16,
                    help=f"wire-field ceiling for the dashed extension "
                         f"(default {MAX_SLOTS_UINT16} = uint16; pass {MAX_SLOTS} "
                         f"to draw only what ships today)")
    args = ap.parse_args()
    render(args.v1, args.v2, args.out, nodes=args.nodes, max_hops=args.max_hops,
           tx_dbm=args.tx_power, source=args.source, marker_sf=args.marker_sf,
           max_slots=args.max_slots)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
