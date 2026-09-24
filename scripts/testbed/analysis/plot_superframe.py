"""Paper figures for the LoRaMesher v2 superframe / throughput-overhead story.

Three figures, complementary to the existing per-SF *outcome* bars
(plot.py / formation_sf_comparison.png) — this is the *structure & capacity*
layer:

  1. Mechanism   — slot composition per SF as a time-stacked bar (bar height =
                   superframe time, so "superframe time vs SF" lives here, not
                   as its own chart). Shows control overhead (= #nodes) dominates.
  2. Pareto      — model-swept capacity vs latency over the data-slots knob, one
                   frontier per SF, colour = energy, the shipped default ringed,
                   measured runs overlaid as anchors. THE headline.
  3. Sensitivity — capacity/overhead vs data_slots (measured-anchored) and
                   capacity/energy vs duty_cycle (model-only axis).

Model curves come from analysis/model.py; measured anchors from
analysis/superframe.py over the existing formation runs.

    python3 scripts/testbed/analysis/plot_superframe.py scripts/testbed/runs/formation
    python3 scripts/testbed/analysis/plot_superframe.py scripts/testbed/runs/formation --simple
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path
from statistics import mean

if __name__ == "__main__":
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis import model
from analysis.multirun import _cell_of
from analysis.superframe import extract

_SF_RE = re.compile(r"SF(\d+)")
# Composition stack: bottom -> top, with colourblind-friendly hues.
_COMPOSITION = [
    ("data", "#4C78A8", "data"),
    ("control", "#F58518", "control"),
    ("discovery", "#E45756", "discovery"),
    ("sync", "#72B7B2", "sync"),
    ("sleep", "#BAB0AC", "sleep"),
]
# Fallback topology for the model frontiers when a SF has no measured runs.
_MODEL_NODES = 16
_MODEL_HOPS = 5
_DATA_SLOT_GRID = [1, 2, 3, 4, 6, 8]


def _topology(measured: dict[int, dict], sf: int) -> tuple[int, int]:
    """Per-SF (node_count, max_hops) from measured runs so model frontiers pass
    through the measured anchor; fall back to a representative 16-node cluster."""
    m = measured.get(sf)
    if not m:
        return _MODEL_NODES, _MODEL_HOPS
    return max(1, round(m["node_count"])), max(0, round(m["max_hops"]))


# ── Measured aggregation over the formation batch ────────────────────────────

def _sf_of_cell(cell: str) -> int | None:
    m = _SF_RE.search(cell)
    return int(m.group(1)) if m else None


def aggregate_measured(batch_dir: Path, drop_first: int = 1) -> dict[int, dict]:
    """Mean superframe structure per SF across a batch's runs (drop r<drop_first)."""
    per_sf: dict[int, list[dict]] = defaultdict(list)
    for run_dir in sorted(batch_dir.iterdir()):
        if not run_dir.is_dir():
            continue
        cr = _cell_of(run_dir)
        if cr is None:
            continue
        cell, rep = cr
        if rep < drop_first:
            continue
        sf = _sf_of_cell(cell)
        if sf is None:
            continue
        rec = extract(run_dir)
        if rec:
            per_sf[sf].append(rec)

    out: dict[int, dict] = {}
    keys = ["total_slots", "sync", "control", "discovery", "data", "sleep",
            "slot_dur_ms", "superframe_s", "overhead_frac", "capacity_Bps",
            "node_count", "max_hops", "data_slots_granted", "recurrence_s"]
    for sf, recs in per_sf.items():
        agg = {k: mean(r[k] for r in recs if r[k] is not None) for k in keys}
        agg["n"] = len(recs)
        out[sf] = agg
    return out


# ── Figure 1: mechanism (time-stacked composition) ───────────────────────────

def fig_mechanism(measured: dict[int, dict], out_path: Path) -> Path:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    sfs = sorted(measured)
    fig, ax = plt.subplots(figsize=(max(6.5, 1.6 * len(sfs) + 2), 5.0))
    x = list(range(len(sfs)))
    for i, sf in enumerate(sfs):
        m = measured[sf]
        slot_s = m["slot_dur_ms"] / 1000.0
        bottom = 0.0
        for key, color, label in _COMPOSITION:
            seg = m[key] * slot_s
            ax.bar(i, seg, bottom=bottom, color=color, edgecolor="white",
                   linewidth=0.5, label=label if i == 0 else None)
            bottom += seg
        ax.text(i, bottom + 0.5, f"{m['superframe_s']:.0f}s\nn={m['n']}",
                ha="center", va="bottom", fontsize=9)

    ax.set_ylim(top=max(measured[sf]["superframe_s"] for sf in sfs) * 1.15)
    ax.set_xticks(x)
    ax.set_xticklabels([f"SF{sf}" for sf in sfs])
    ax.set_xlabel("Spreading Factor")
    ax.set_ylabel("Superframe time (s), by slot type")
    ax.set_title("Superframe composition & duration per SF")
    ax.legend(loc="upper left", fontsize=9, framealpha=0.9)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"wrote {out_path}")
    return out_path


# ── Figure 2: capacity vs latency Pareto, swept over data_slots ──────────────

def fig_pareto(measured: dict[int, dict], out_path: Path) -> Path:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D

    sfs = sorted(measured) or [7, 9, 12]
    fig, ax = plt.subplots(figsize=(8.0, 5.6))

    # Model frontier per SF, anchored to that SF's measured topology so the
    # data_slots=1 point coincides with the measured star.
    grid = {}
    for sf in sfs:
        n, h = _topology(measured, sf)
        grid[sf] = [model.metrics(sf, n, ds, 0.1, max_hops=h) for ds in _DATA_SLOT_GRID]

    sc = None
    for sf in sfs:
        pts = grid[sf]
        xs = [p["capacity_Bps"] for p in pts]
        ys = [p["recurrence_s"] for p in pts]
        ax.plot(xs, ys, "-", color="#999999", lw=1.0, zorder=1)
        sc = ax.scatter(xs, ys, c=_DATA_SLOT_GRID, cmap="viridis",
                        vmin=min(_DATA_SLOT_GRID), vmax=max(_DATA_SLOT_GRID),
                        s=70, edgecolor="black", linewidth=0.5, zorder=3)
        ax.annotate(f"SF{sf}", (xs[0], ys[0]), fontsize=9, fontweight="bold",
                    xytext=(6, 6), textcoords="offset points")
        # Ring the shipped default (data_slots=1).
        ax.scatter([xs[0]], [ys[0]], s=240, facecolors="none",
                   edgecolors="#D62728", linewidth=1.8, zorder=4)
        # Mark where the max_data_slots pool starts capping throughput.
        for ds, p in zip(_DATA_SLOT_GRID, pts):
            if p["data_slots_granted"] < ds - 1e-6:
                ax.scatter([p["capacity_Bps"]], [p["recurrence_s"]], marker="x",
                           color="#D62728", s=70, linewidth=1.8, zorder=5)
                break

    # Measured anchors (formation = data_slots 1, duty 0.1).
    for sf, m in measured.items():
        ax.scatter([m["capacity_Bps"]], [m["recurrence_s"]], marker="*",
                   s=260, color="gold", edgecolor="black", linewidth=0.8, zorder=6)

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Per-node capacity (B/s)  →  better")
    ax.set_ylabel("Data-slot recurrence ≈ latency (s)  ←  better")
    ax.set_title("Capacity vs latency across the data-slot knob, per SF\n"
                 "(more data slots → both improve, until the node-pool cap)",
                 fontsize=11)
    cbar = fig.colorbar(sc, ax=ax, ticks=_DATA_SLOT_GRID)
    cbar.set_label("Data slots per node (knob)")
    legend = [
        Line2D([0], [0], marker="o", color="w", markerfacecolor="#4C78A8",
               markeredgecolor="black", markersize=9, label="model (data_slots 1→8)"),
        Line2D([0], [0], marker="o", color="w", markerfacecolor="none",
               markeredgecolor="#D62728", markersize=13, label="shipped default (1 slot)"),
        Line2D([0], [0], marker="x", color="#D62728", linestyle="none",
               markersize=8, label="max_data_slots pool cap"),
        Line2D([0], [0], marker="*", color="gold", markeredgecolor="black",
               linestyle="none", markersize=14, label="measured (formation runs)"),
    ]
    ax.legend(handles=legend, loc="upper right", fontsize=8, framealpha=0.95)
    ax.grid(True, which="both", linestyle=":", alpha=0.4)
    ax.set_axisbelow(True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"wrote {out_path}")
    return out_path


# ── Figure 3: knob sensitivity (2 panels) ────────────────────────────────────

def fig_sensitivity(measured: dict[int, dict], out_path: Path, sf: int = 9) -> Path:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, (axA, axB) = plt.subplots(1, 2, figsize=(11.0, 4.8))
    n_nodes, max_hops = _topology(measured, sf)

    # Panel A — vs data_slots (duty fixed at 0.1); measured-anchored.
    ds_pts = [model.metrics(sf, n_nodes, ds, 0.1, max_hops=max_hops)
              for ds in _DATA_SLOT_GRID]
    cap = [p["capacity_Bps"] for p in ds_pts]
    oh = [p["overhead_frac"] * 100 for p in ds_pts]
    axA.plot(_DATA_SLOT_GRID, cap, "o-", color="#4C78A8", label="capacity")
    axA.set_xlabel("Data slots per node")
    axA.set_ylabel("Per-node capacity (B/s)", color="#4C78A8")
    axA.tick_params(axis="y", labelcolor="#4C78A8")
    axA2 = axA.twinx()
    axA2.plot(_DATA_SLOT_GRID, oh, "s--", color="#E45756", label="overhead %")
    axA2.set_ylabel("Overhead (%)", color="#E45756")
    axA2.tick_params(axis="y", labelcolor="#E45756")
    if sf in measured:
        m = measured[sf]
        axA.scatter([1], [m["capacity_Bps"]], marker="*", s=240, color="gold",
                    edgecolor="black", zorder=6, label="measured")
    axA.set_title("Data slots: throughput ↑, overhead ↓", fontsize=10)
    axA.grid(axis="y", linestyle=":", alpha=0.5)

    # Panel B — vs duty_cycle (data_slots fixed at 1); model-only axis. The grid
    # reaches the firmware default 1%, where the frame becomes sleep-padded
    # (above ~5% a busy network is already active-bound, so the knob is moot).
    duties = [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0]
    duty_pts = [model.metrics(sf, n_nodes, 1, d, max_hops=max_hops) for d in duties]
    capd = [p["capacity_Bps"] for p in duty_pts]
    mAd = [p["mean_current_mA"] for p in duty_pts]
    xd = [d * 100 for d in duties]
    axB.plot(xd, capd, "o-", color="#4C78A8", label="capacity")
    axB.set_xscale("log")
    axB.set_xlabel("Target TX duty cycle (%, log)")
    axB.set_ylabel("Per-node capacity (B/s)", color="#4C78A8")
    axB.tick_params(axis="y", labelcolor="#4C78A8")
    axB2 = axB.twinx()
    axB2.plot(xd, mAd, "s--", color="#54A24B", label="energy")
    axB2.set_ylabel("Mean current (mA)", color="#54A24B")
    axB2.tick_params(axis="y", labelcolor="#54A24B")
    axB.set_title("Duty cycle: low duty pads sleep → throughput & energy ↓",
                  fontsize=10)
    axB.grid(axis="y", linestyle=":", alpha=0.5)

    fig.suptitle(f"SF{sf} knob sensitivity (model curves; ★ = measured)",
                 fontsize=12)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"wrote {out_path}")
    return out_path


# ── Simple fallback: superframe time vs SF bars ──────────────────────────────

def fig_simple(measured: dict[int, dict], out_path: Path) -> Path:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    sfs = sorted(measured)
    fig, ax = plt.subplots(figsize=(max(6, 1.4 * len(sfs) + 2), 4.6))
    vals = [measured[sf]["superframe_s"] for sf in sfs]
    bars = ax.bar(range(len(sfs)), vals, color="#4C78A8", edgecolor="black",
                  linewidth=0.6)
    for i, sf in enumerate(sfs):
        m = measured[sf]
        ax.text(i, vals[i], f"{m['slot_dur_ms']}ms×{m['total_slots']:.0f}",
                ha="center", va="bottom", fontsize=9)
    ax.set_xticks(range(len(sfs)))
    ax.set_xticklabels([f"SF{sf}" for sf in sfs])
    ax.set_xlabel("Spreading Factor")
    ax.set_ylabel("Superframe time (s)")
    ax.set_title("Superframe time per SF")
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"wrote {out_path}")
    return out_path


def main() -> int:
    ap = argparse.ArgumentParser(description="Superframe / throughput-overhead figures")
    ap.add_argument("batch_dir", type=Path, help="e.g. scripts/testbed/runs/formation")
    ap.add_argument("--drop-first", type=int, default=1,
                    help="Skip the first N reps per cell (cold start). Default 1.")
    ap.add_argument("--simple", action="store_true",
                    help="Only the plain superframe-time-vs-SF bar chart.")
    ap.add_argument("--sf", type=int, default=9, help="SF for the sensitivity panels.")
    args = ap.parse_args()

    measured = aggregate_measured(args.batch_dir, drop_first=args.drop_first)
    if not measured:
        raise SystemExit(f"no extractable superframe data under {args.batch_dir}")
    print(f"aggregated SF cells: "
          + ", ".join(f"SF{sf}(n={measured[sf]['n']})" for sf in sorted(measured)))

    if args.simple:
        fig_simple(measured, args.batch_dir / "superframe_time_per_sf.png")
        return 0

    fig_mechanism(measured, args.batch_dir / "superframe_composition.png")
    fig_pareto(measured, args.batch_dir / "superframe_pareto.png")
    fig_sensitivity(measured, args.batch_dir / "superframe_sensitivity.png", sf=args.sf)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
