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
    _UNCAPPED_POOL,
    FIG_WIDE,
    _set_pub_style,
    _sf_topology,
    save_fig,
)


def panel_dataslots(anchors: dict, out_dir: Path, sf: int) -> None:
    """Panel A — per-node capacity ↑ and control overhead ↓ vs data slots."""
    import matplotlib.pyplot as plt

    n, hops, _pool = _sf_topology(anchors, sf)
    fig, axA = plt.subplots(figsize=FIG_WIDE)

    ds_pts = [model.metrics(sf, n, ds, 0.1, max_hops=hops, max_data_slots=_UNCAPPED_POOL)
              for ds in _DATA_SLOT_GRID]
    cap = [p["capacity_Bps"] for p in ds_pts]
    oh = [p["overhead_frac"] * 100 for p in ds_pts]
    axA.plot(_DATA_SLOT_GRID, cap, "o-", color="#4C78A8", label="capacity (model)")
    axA.set_xlabel("Data slots per node")
    axA.set_ylabel("Per-node capacity ceiling (B/s)", color="#4C78A8")
    axA.tick_params(axis="y", labelcolor="#4C78A8")
    axA2 = axA.twinx()
    axA2.plot(_DATA_SLOT_GRID, oh, "s--", color="#E45756", label="overhead % (model)")
    axA2.set_ylabel("Overhead (%)", color="#E45756")
    axA2.tick_params(axis="y", labelcolor="#E45756")
    # Measured stars at every data-slot count we ran at this SF.
    first = True
    for (s, ds), m in sorted(anchors.items()):
        if s != sf:
            continue
        axA.scatter([ds], [m["capacity_Bps"]], marker="*", s=90, color="gold",
                    edgecolor="black", zorder=6, label="measured" if first else None)
        axA2.scatter([ds], [m["overhead_frac"] * 100], marker="*", s=90,
                     color="#9467bd", edgecolor="black", zorder=6)
        first = False
    axA.grid(axis="y", linestyle=":", alpha=0.5)
    h1, l1 = axA.get_legend_handles_labels()
    h2, l2 = axA2.get_legend_handles_labels()
    axA.legend(h1 + h2, l1 + l2, loc="center right", framealpha=0.9)

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
