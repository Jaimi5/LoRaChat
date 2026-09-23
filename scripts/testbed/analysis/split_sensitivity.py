"""Split the two-panel ``superframe_sensitivity`` figure into standalone panels.

``plot_paper.py:fig_sensitivity`` draws two panels side-by-side under one
suptitle. For LaTeX subfigures it is handier to have each panel as its own file.
This regenerates them from the same measured anchors and publication styling, so
the split panels are pixel-for-pixel the same content — just without the shared
suptitle and with each panel's own title standing alone.

Outputs (PDF + SVG + PNG, matching the rest of the figure set):
    superframe_sensitivity_A.{pdf,svg,png}   — capacity/overhead vs data slots
    superframe_sensitivity_B.{pdf,svg,png}   — capacity/energy vs duty cycle

Usage (same batches/flags as plot_paper.py):
    python3 scripts/testbed/analysis/split_sensitivity.py \\
        --batch scripts/testbed/runs/formation \\
        --batch scripts/testbed/runs/dataslots_check \\
        --out   scripts/testbed/runs/figures --sf 9
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __name__ == "__main__":
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis import model
from analysis.superframe import aggregate_anchors
from analysis.plot_paper import (
    _DATA_SLOT_GRID,
    _DUTY_GRID,
    FIG_WIDE,
    _set_pub_style,
    _sf_topology,
    save_fig,
)


def _measured_wait(a: dict) -> float:
    """Expected slot-access wait implied by a MEASURED superframe anchor.

    Same closed form as model.metrics()["expected_wait_s"], but driven by the
    frame the Network Manager actually logged rather than the model, so the
    anchors are measurements and not the curve restated.
    """
    T = a["superframe_s"]
    S = a["slot_dur_ms"] / 1000.0
    n = a["data_slots_granted"]
    if not (T and n):
        return float("nan")
    gap = T - (n - 1) * S
    return (gap ** 2 + (n - 1) * S ** 2) / (2 * T)


def panel_dataslots(anchors: dict, out_dir: Path, sf: int) -> None:
    """Panel A — what the per-node data-slot request actually buys.

    Replaces the earlier capacity-vs-overhead panel. That one plotted two
    curves that are not independent: at fixed SF the capacity ceiling is
    s*L_max/T_sf and the recurrence is T_sf/s, so capacity == L_max/recurrence
    exactly and the pair carries a single degree of freedom drawn twice.

    This panel plots the two quantities that genuinely diverge, both in
    seconds so they share one axis:

      - data-slot recurrence T_sf/s, FALLING. A rate: goodput follows it.
      - expected wait for the next data slot, RISING. The allocator grants a
        node its slots contiguously (network_service.cpp:2219-2239), so extra
        slots arrive as a longer burst behind a longer idle gap. For periodic
        single-packet traffic -- the delivery experiment's regime -- more slots
        means MORE delay, not less.
      - the wait the same slot count would give if the slots were interleaved,
        as the delay the current layout forfeits.

    It also honours the real data-slot pool. The previous panel swept
    _UNCAPPED_POOL (10_000), so its right-hand third described a configuration
    the firmware cannot reach; past the pool the request buys nothing at all,
    which is a clamp rather than a diminishing return.
    """
    import matplotlib.pyplot as plt

    n, hops, pool = _sf_topology(anchors, sf)
    fig, ax = plt.subplots(figsize=FIG_WIDE)

    pts = [model.metrics(sf, n, ds, 0.1, max_hops=hops, max_data_slots=pool)
           for ds in _DATA_SLOT_GRID]
    recur = [p["recurrence_s"] for p in pts]
    wait = [p["expected_wait_s"] for p in pts]
    wait_il = [p["expected_wait_interleaved_s"] for p in pts]

    ax.plot(_DATA_SLOT_GRID, wait, "o-", color="#E45756", zorder=4,
            label="wait for next data slot (as shipped)")
    ax.plot(_DATA_SLOT_GRID, recur, "s-", color="#4C78A8", zorder=4,
            label="data-slot recurrence (rate)")
    ax.plot(_DATA_SLOT_GRID, wait_il, ":", color="#666666", lw=1.4, zorder=3,
            label="wait if slots were interleaved")

    # Shade the region the data-slot pool makes unreachable: past this the
    # request is clamped and buys neither rate nor delay.
    capped = [ds for ds, p in zip(_DATA_SLOT_GRID, pts)
              if p["data_slots_granted"] < ds - 1e-6]
    ax.set_ylim(0, max(wait) * 1.28)
    if capped:
        ax.axvspan(min(capped), max(_DATA_SLOT_GRID), color="#999999",
                   alpha=0.13, zorder=0, lw=0)
        ax.text(min(capped) + 0.1, ax.get_ylim()[1] * 0.985,
                f"clamped by the\ndata-slot pool ({pool})", fontsize=5.5,
                color="#555555", va="top", ha="left", zorder=1, linespacing=1.25)

    # Measured anchors, from the logged superframes rather than the model.
    first = True
    for (s_, ds), m in sorted(anchors.items()):
        if s_ != sf:
            continue
        ax.scatter([ds], [m["recurrence_s"]], marker="*", s=90, color="gold",
                   edgecolor="black", zorder=6,
                   label="measured superframe" if first else None)
        ax.scatter([ds], [_measured_wait(m)], marker="*", s=90, color="gold",
                   edgecolor="black", zorder=6)
        first = False

    ax.set_xlabel("Data slots per node")
    ax.set_ylabel("Seconds")
    ax.set_title(f"SF{sf}, {n} nodes: more slots raise the rate but lengthen the wait",
                 fontsize=8)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    ax.legend(loc="lower center", framealpha=0.92, fontsize=6, ncol=2,
              handlelength=1.8, columnspacing=1.1, borderpad=0.5)

    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_sensitivity_A")


def panel_duty(anchors: dict, out_dir: Path, sf: int,
               tx_power_dbm: float = 2.0) -> None:
    """Panel B — per-node capacity and mean current vs target TX duty cycle.

    `tx_power_dbm` defaults to the testbed's dense-cluster setting rather than
    the firmware default, so the current axis is comparable with the measured
    energy figures this panel sits beside.
    """
    import matplotlib.pyplot as plt

    n, hops, pool = _sf_topology(anchors, sf)
    fig, axB = plt.subplots(figsize=(5.6, 4.4))

    duty_pts = [model.metrics(sf, n, 1, d, max_hops=hops, max_data_slots=pool,
                              tx_power_dbm=tx_power_dbm)
                for d in _DUTY_GRID]
    capd = [p["capacity_Bps"] for p in duty_pts]
    mAd = [p["mean_current_mA"] for p in duty_pts]
    xd = [d * 100 for d in _DUTY_GRID]
    axB.plot(xd, capd, "o-", color="#4C78A8", label="capacity")
    axB.set_xscale("log")
    axB.set_xlabel("Target TX duty cycle (%, log)")
    axB.set_ylabel("Per-node capacity ceiling (B/s)", color="#4C78A8")
    axB.tick_params(axis="y", labelcolor="#4C78A8")
    axB2 = axB.twinx()
    axB2.plot(xd, mAd, "s--", color="#54A24B", label="energy")
    axB2.set_ylabel("Mean current (mA)", color="#54A24B")
    axB2.tick_params(axis="y", labelcolor="#54A24B")
    axB.set_title(f"SF{sf} duty cycle (model only): low duty pads sleep")
    axB.grid(axis="y", linestyle=":", alpha=0.5)
    h1, l1 = axB.get_legend_handles_labels()
    h2, l2 = axB2.get_legend_handles_labels()
    axB.legend(h1 + h2, l1 + l2, loc="center right", framealpha=0.9)

    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_sensitivity_B")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Split superframe_sensitivity into two standalone panels.")
    ap.add_argument("--batch", action="append", type=Path, required=True,
                    metavar="DIR", help="A run-batch directory (repeatable).")
    ap.add_argument("--out", type=Path, required=True,
                    help="Output directory for the figures (created if absent).")
    ap.add_argument("--sf", type=int, default=9, help="SF for the panels. Default 9.")
    ap.add_argument("--tx-power", type=float, default=2.0, metavar="DBM",
                    help="TX power for the energy panel (default 2 dBm, the "
                         "testbed's dense-cluster setting).")
    ap.add_argument("--drop-first", type=int, default=1,
                    help="Skip the first N reps per cell (cold start). Default 1.")
    ap.add_argument("--keep-partial", action="store_true",
                    help="Keep runs that did not reach full membership for their SF.")
    ap.add_argument("--frame-selection", choices=("modal", "mean"), default="modal",
                    help="Per cell, report the modal converged frame (default) or "
                         "the mean over every run. Must match plot_paper.py, or "
                         "panel A's measured stars disagree with the composition "
                         "figure drawn from the same anchors.")
    args = ap.parse_args()

    anchors = aggregate_anchors(args.batch, drop_first=args.drop_first,
                                require_full_membership=not args.keep_partial,
                                frame_selection=args.frame_selection)
    if not anchors:
        raise SystemExit(f"no extractable superframe anchors under {args.batch}")

    _set_pub_style()
    panel_dataslots(anchors, args.out, sf=args.sf)
    panel_duty(anchors, args.out, sf=args.sf, tx_power_dbm=args.tx_power)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
