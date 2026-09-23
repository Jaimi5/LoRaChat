"""Publication figure set for the LoRaMesher v2 superframe / data-slot study.

This is the *multi-batch combiner* companion to ``plot_superframe.py``: it merges
the measured anchors from several automatic-run batches into one design-space
picture, so the data-slot axis that ``plot_superframe.py`` could only draw as a
model curve is now empirically anchored at the data-slot counts that were
actually run (e.g. ds=1 from ``formation`` + ds=2,4 from ``dataslots_check``).

Model curves come from analysis/model.py; measured anchors from
analysis/superframe.py:aggregate_anchors() over every supplied batch, keyed by
(spreading factor, data_slots). Every figure is written as PDF + SVG + PNG with
publication styling (embedded TrueType fonts, serif text).

Five figures (into --out):
  1. superframe_composition          — slot composition per SF (measured, ds=1).
  2. superframe_dataslots_composition — composition vs data_slots at one SF
                                        (measured ds=1,2,4): overhead dilution.
  3. superframe_pareto               — capacity vs latency model frontier per SF,
                                        with the measured data-slot anchors
                                        walking the frontier. THE headline.
  4. superframe_sensitivity          — capacity/overhead vs data_slots (measured-
                                        anchored) + capacity/energy vs duty (model).
  5. superframe_validation           — model-predicted vs measured (capacity,
                                        overhead, superframe time, recurrence) at
                                        each measured data-slot count.
  6. superframe_scalability_vs_n      — analytical projection (model only) of
                                        capacity / overhead / superframe duration
                                        vs network size N, anchored at measured N.

Usage:
    python3 scripts/testbed/analysis/plot_paper.py \\
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

# Composition stack: bottom -> top, colourblind-friendly hues (shared with
# plot_superframe.py so both figure families read the same).
_COMPOSITION = [
    ("data", "#4C78A8", "data"),
    ("control", "#F58518", "control"),
    ("discovery", "#E45756", "discovery"),
    ("sync", "#72B7B2", "sync"),
    ("sleep", "#BAB0AC", "sleep"),
]
_DATA_SLOT_GRID = [1, 2, 3, 4, 6, 8]
_DUTY_GRID = [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0]

# Canvas presets matching the paper's figure containers, so every figure is drawn
# at the size it is printed at and \includegraphics never rescales the text.
# main.tex: a4paper with 20 mm margins -> \linewidth = 481.9 pt = 6.69 in.
FIG_FULL = (6.69, 3.60)   # \includegraphics[width=\linewidth]
FIG_WIDE = (4.01, 2.90)   # \includegraphics[width=0.6\linewidth]      -> 289 pt
FIG_HALF = (3.28, 2.40)   # width=\linewidth in a 0.49\linewidth minipage -> 236 pt


# ── Publication styling + multi-format save ──────────────────────────────────

def _set_pub_style() -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({
        "font.family": "serif",
        "font.serif": ["Times New Roman", "Nimbus Roman", "DejaVu Serif"],
        "mathtext.fontset": "stix",
        # Sized for the FIG_* canvases above, which are drawn at final print size:
        # what is set here is what appears on the page.
        "font.size": 8,
        "axes.titlesize": 9,
        "axes.labelsize": 8,
        "legend.fontsize": 7,
        "xtick.labelsize": 7,
        "ytick.labelsize": 7,
        "figure.dpi": 150,
        "savefig.dpi": 300,
        "savefig.bbox": "tight",
        # Embed fonts as TrueType so the publisher's toolchain keeps text live.
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
        "svg.fonttype": "none",
    })


def save_fig(fig, out_dir: Path, stem: str) -> None:
    """Write one figure as PDF (vector), SVG (vector), and PNG (raster)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    for ext in ("pdf", "svg", "png"):
        path = out_dir / f"{stem}.{ext}"
        fig.savefig(path)
        print(f"  wrote {path}")
    import matplotlib.pyplot as plt
    plt.close(fig)


# ── Model anchoring helpers ──────────────────────────────────────────────────

def _sf_topology(anchors: dict, sf: int) -> tuple[int, int, int]:
    """(node_count, max_hops, max_data_slots pool) for a SF's model frontier,
    taken from that SF's measured anchors so the model passes through them."""
    recs = [a for (s, _ds), a in anchors.items() if s == sf]
    if not recs:
        return 16, 5, model.DEFAULT_MAX_DATA_SLOTS
    n = max(a["node_count_max"] for a in recs)
    hops = max(1, round(sum(a["max_hops"] for a in recs) / len(recs)))
    pools = [a["max_data_slots_cfg"] for a in recs if a["max_data_slots_cfg"]]
    pool = max(pools) if pools else model.DEFAULT_MAX_DATA_SLOTS
    return int(round(n)), int(hops), int(pool)


# ── Figure 1: composition per SF (measured, ds=1) ────────────────────────────

def fig_composition(anchors: dict, out_dir: Path) -> None:
    import matplotlib.pyplot as plt

    sfs = sorted({sf for (sf, ds) in anchors if ds == 1})
    cells = [anchors[(sf, 1)] for sf in sfs]
    fig, ax = plt.subplots(figsize=FIG_HALF)
    for i, (sf, m) in enumerate(zip(sfs, cells)):
        slot_s = m["slot_dur_ms"] / 1000.0
        bottom = 0.0
        for key, color, label in _COMPOSITION:
            seg = m[key] * slot_s
            ax.bar(i, seg, bottom=bottom, color=color, edgecolor="white",
                   linewidth=0.5, label=label if i == 0 else None)
            bottom += seg
        ax.text(i, bottom + 0.5, f"{m['superframe_s']:.0f}s",
                ha="center", va="bottom", fontsize=7)

    ax.set_ylim(top=max(c["superframe_s"] for c in cells) * 1.18)
    ax.set_xticks(range(len(sfs)))
    ax.set_xticklabels([f"SF{sf}" for sf in sfs])
    ax.set_xlabel("Spreading factor")
    ax.set_ylabel("Superframe time (s), by slot type")
    ax.legend(loc="upper left", framealpha=0.9)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_composition")


# ── Figure 2: composition vs data_slots (mechanism) ──────────────────────────

def fig_dataslots_composition(anchors: dict, out_dir: Path, sf: int) -> None:
    import matplotlib.pyplot as plt

    dss = sorted(ds for (s, ds) in anchors if s == sf)
    if len(dss) < 2:
        print(f"  [skip] dataslots_composition: SF{sf} has <2 data-slot anchors")
        return
    cells = [anchors[(sf, ds)] for ds in dss]
    fig, ax = plt.subplots(figsize=FIG_HALF)
    for i, (ds, m) in enumerate(zip(dss, cells)):
        slot_s = m["slot_dur_ms"] / 1000.0
        bottom = 0.0
        for key, color, label in _COMPOSITION:
            seg = m[key] * slot_s
            ax.bar(i, seg, bottom=bottom, color=color, edgecolor="white",
                   linewidth=0.5, label=label if i == 0 else None)
            bottom += seg
        ax.text(i, bottom + 0.5,
                f"{m['superframe_s']:.0f}s\novh {m['overhead_frac']*100:.0f}%",
                ha="center", va="bottom", fontsize=7)

    ax.set_ylim(top=max(c["superframe_s"] for c in cells) * 1.18)
    ax.set_xticks(range(len(dss)))
    ax.set_xticklabels([f"{ds}" for ds in dss])
    ax.set_xlabel("Data slots per node")
    ax.set_ylabel("Superframe time (s), by slot type")
    ax.legend(loc="upper left", framealpha=0.9)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_dataslots_composition")


# ── Figure 3: capacity vs latency Pareto (headline) ──────────────────────────

def fig_pareto(anchors: dict, out_dir: Path) -> None:
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D

    sfs = sorted({sf for (sf, _ds) in anchors})
    fig, ax = plt.subplots(figsize=(7.2, 5.2))

    sc = None
    for sf in sfs:
        n, hops, pool = _sf_topology(anchors, sf)
        pts = [model.metrics(sf, n, ds, 0.1, max_hops=hops, max_data_slots=pool)
               for ds in _DATA_SLOT_GRID]
        xs = [p["capacity_Bps"] for p in pts]
        ys = [p["recurrence_s"] for p in pts]
        ax.plot(xs, ys, "-", color="#999999", lw=1.0, zorder=1)
        sc = ax.scatter(xs, ys, c=_DATA_SLOT_GRID, cmap="viridis",
                        vmin=min(_DATA_SLOT_GRID), vmax=max(_DATA_SLOT_GRID),
                        s=64, edgecolor="black", linewidth=0.5, zorder=3)
        ax.annotate(f"SF{sf}", (xs[0], ys[0]), fontsize=9, fontweight="bold",
                    xytext=(6, 6), textcoords="offset points")
        ax.scatter([xs[0]], [ys[0]], s=230, facecolors="none",
                   edgecolors="#D62728", linewidth=1.6, zorder=4)
        for ds, p in zip(_DATA_SLOT_GRID, pts):
            if p["data_slots_granted"] < ds - 1e-6:
                ax.scatter([p["capacity_Bps"]], [p["recurrence_s"]], marker="x",
                           color="#D62728", s=64, linewidth=1.8, zorder=5)
                break

    # Measured anchors (one star per (SF, data_slots) cell).
    for (sf, ds), m in sorted(anchors.items()):
        ax.scatter([m["capacity_Bps"]], [m["recurrence_s"]], marker="*",
                   s=240, color="gold", edgecolor="black", linewidth=0.8, zorder=6)
        ax.annotate(f"{ds}", (m["capacity_Bps"], m["recurrence_s"]), fontsize=7,
                    ha="center", va="center", zorder=7)

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("Per-node capacity ceiling (B/s)  →  better")
    ax.set_ylabel("Data-slot recurrence (s, structural)  ←  better")
    ax.set_title("Capacity vs data-slot recurrence across the knob, per SF\n"
                 "(more data slots → both improve, until the pool cap)")
    cbar = fig.colorbar(sc, ax=ax, ticks=_DATA_SLOT_GRID)
    cbar.set_label("Data slots per node (knob)")
    legend = [
        Line2D([0], [0], marker="o", color="w", markerfacecolor="#4C78A8",
               markeredgecolor="black", markersize=9, label="model (ds 1→8)"),
        Line2D([0], [0], marker="o", color="w", markerfacecolor="none",
               markeredgecolor="#D62728", markersize=13, label="shipped default (1 slot)"),
        Line2D([0], [0], marker="x", color="#D62728", linestyle="none",
               markersize=8, label="max_data_slots pool cap"),
        Line2D([0], [0], marker="*", color="gold", markeredgecolor="black",
               linestyle="none", markersize=14, label="measured (digit = ds/node)"),
    ]
    ax.legend(handles=legend, loc="upper right", framealpha=0.95)
    ax.grid(True, which="both", linestyle=":", alpha=0.4)
    ax.set_axisbelow(True)
    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_pareto")


# ── Figure 4: knob sensitivity (2 panels) ────────────────────────────────────

def fig_sensitivity(anchors: dict, out_dir: Path, sf: int) -> None:
    import matplotlib.pyplot as plt

    n, hops, pool = _sf_topology(anchors, sf)
    fig, (axA, axB) = plt.subplots(1, 2, figsize=(10.5, 4.4))

    # Panel A — vs data_slots (duty fixed 0.1), measured-anchored.
    #
    # Capacity and recurrence are the SAME curve at fixed SF (capacity =
    # L_max/recurrence exactly), so plotting both says nothing twice. What
    # actually diverges is the rate against the delay: the allocator grants a
    # node its slots contiguously (network_service.cpp:2219-2239), so extra
    # slots arrive as a longer burst behind a longer gap and the wait GROWS
    # while the recurrence falls. See split_sensitivity.panel_dataslots, which
    # renders this same panel for the paper.
    ds_pts = [model.metrics(sf, n, ds, 0.1, max_hops=hops, max_data_slots=pool)
              for ds in _DATA_SLOT_GRID]
    recur = [p["recurrence_s"] for p in ds_pts]
    wait = [p["expected_wait_s"] for p in ds_pts]
    wait_il = [p["expected_wait_interleaved_s"] for p in ds_pts]
    axA.plot(_DATA_SLOT_GRID, wait, "o-", color="#E45756", zorder=4,
             label="wait for next data slot (as shipped)")
    axA.plot(_DATA_SLOT_GRID, recur, "s-", color="#4C78A8", zorder=4,
             label="data-slot recurrence (rate)")
    axA.plot(_DATA_SLOT_GRID, wait_il, ":", color="#666666", lw=1.4, zorder=3,
             label="wait if slots were interleaved")
    axA.set_xlabel("Data slots per node")
    axA.set_ylabel("Seconds")
    capped = [ds for ds, p in zip(_DATA_SLOT_GRID, ds_pts)
              if p["data_slots_granted"] < ds - 1e-6]
    axA.set_ylim(0, max(wait) * 1.28)
    if capped:
        axA.axvspan(min(capped), max(_DATA_SLOT_GRID), color="#999999",
                    alpha=0.13, zorder=0, lw=0)
    # Measured stars at every data-slot count we ran at this SF.
    first = True
    for (s, ds), m in sorted(anchors.items()):
        if s != sf:
            continue
        axA.scatter([ds], [m["recurrence_s"]], marker="*", s=200, color="gold",
                    edgecolor="black", zorder=6,
                    label="measured superframe" if first else None)
        T, S_, k = m["superframe_s"], m["slot_dur_ms"] / 1000.0, m["data_slots_granted"]
        if T and k:
            axA.scatter([ds], [((T - (k - 1) * S_) ** 2 + (k - 1) * S_ ** 2) / (2 * T)],
                        marker="*", s=200, color="gold", edgecolor="black", zorder=6)
        first = False
    axA.set_title(f"SF{sf}: more slots raise the rate but lengthen the wait")
    axA.grid(axis="y", linestyle=":", alpha=0.5)
    axA.set_axisbelow(True)
    axA.legend(loc="lower center", framealpha=0.92, fontsize=7, ncol=2)

    # Panel B — vs duty_cycle (data_slots fixed 1), model-only axis.
    duty_pts = [model.metrics(sf, n, 1, d, max_hops=hops, max_data_slots=pool)
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

    fig.suptitle(f"SF{sf} knob sensitivity (curves = model; gold = measured)",
                 fontsize=12)
    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_sensitivity")


# ── Figure 5: model validation (predicted vs measured) ───────────────────────

_VAL_METRICS = [
    ("capacity_Bps", "Per-node capacity ceiling (B/s)", 1.0),
    ("overhead_frac", "Control overhead (%)", 100.0),
    ("superframe_s", "Superframe time (s)", 1.0),
    ("recurrence_s", "Recurrence (s)", 1.0),
]


def fig_validation(anchors: dict, out_dir: Path, sf: int) -> float:
    """Grouped bars of model vs measured at each measured ds. Returns max |%err|."""
    import matplotlib.pyplot as plt

    dss = sorted(ds for (s, ds) in anchors if s == sf)
    if not dss:
        print(f"  [skip] validation: no SF{sf} anchors")
        return 0.0

    # Per-anchor model prediction at that anchor's exact firmware inputs.
    preds = {}
    for ds in dss:
        m = anchors[(sf, ds)]
        n = int(round(m["node_count"]))
        hops = max(1, int(round(m["max_hops"])))
        pool = int(m["max_data_slots_cfg"] or model.DEFAULT_MAX_DATA_SLOTS)
        duty = m["duty_cycle"] or 0.1
        mp = int(round(m["max_packet_bytes"]))
        preds[ds] = model.metrics(sf, n, ds, duty, max_hops=hops,
                                  max_data_slots=pool, max_packet=mp)

    fig, axes = plt.subplots(1, len(_VAL_METRICS),
                             figsize=(3.0 * len(_VAL_METRICS), 3.8))
    max_err = 0.0
    width = 0.36
    xs = list(range(len(dss)))
    for ax, (key, label, scale) in zip(axes, _VAL_METRICS):
        meas = [anchors[(sf, ds)][key] * scale for ds in dss]
        modl = [preds[ds][key] * scale for ds in dss]
        ax.bar([x - width / 2 for x in xs], meas, width, label="measured",
               color="gold", edgecolor="black", linewidth=0.5)
        ax.bar([x + width / 2 for x in xs], modl, width, label="model",
               color="#4C78A8", edgecolor="black", linewidth=0.5)
        for ds, mv, pv in zip(dss, meas, modl):
            if mv:
                err = abs(pv - mv) / abs(mv) * 100.0
                max_err = max(max_err, err)
        ax.set_xticks(xs)
        ax.set_xticklabels([f"{ds}" for ds in dss])
        ax.set_xlabel("data slots/node")
        ax.set_title(label, fontsize=9)
        ax.grid(axis="y", linestyle=":", alpha=0.5)
        ax.set_axisbelow(True)
    axes[0].legend(loc="upper left", framealpha=0.9)
    fig.suptitle(f"SF{sf} model validation along the data-slot axis "
                 f"(max error {max_err:.0f}%)", fontsize=12)
    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_validation")
    return max_err


# ── Figure 6: analytical scalability vs network size N (model only) ───────────

def fig_scalability_vs_n(anchors: dict, out_dir: Path, sf: int) -> None:
    """Model projection of capacity / overhead / superframe duration vs node
    count N. Purely analytical (no measurement beyond the single anchored N);
    the measured operating point is marked for reference."""
    import matplotlib.pyplot as plt

    # Anchor the projection to the SF's measured topology (hop depth, pool) so
    # the curve passes through the regime we actually measured.
    base = anchors.get((sf, 1))
    hops = max(1, int(round(base["max_hops"]))) if base else 5
    pool = int(base["max_data_slots_cfg"] or model.DEFAULT_MAX_DATA_SLOTS) if base \
        else model.DEFAULT_MAX_DATA_SLOTS
    n_meas = int(round(base["node_count"])) if base else None

    ns = list(range(2, 41))
    pts = [model.metrics(sf, n, 1, 0.1, max_hops=hops, max_data_slots=pool)
           for n in ns]
    cap = [p["capacity_Bps"] for p in pts]
    oh = [p["overhead_frac"] * 100 for p in pts]
    sframe = [p["superframe_s"] for p in pts]

    fig, (axA, axB) = plt.subplots(1, 2, figsize=(10.5, 4.4))

    # Panel A — capacity ceiling (left) and control overhead (right) vs N.
    axA.plot(ns, cap, "-", color="#4C78A8", label="capacity ceiling")
    axA.set_xlabel("Network size $N$ (nodes)")
    axA.set_ylabel("Per-node capacity ceiling (B/s)", color="#4C78A8")
    axA.tick_params(axis="y", labelcolor="#4C78A8")
    axA2 = axA.twinx()
    axA2.plot(ns, oh, "--", color="#E45756", label="control overhead %")
    axA2.set_ylabel("Control overhead (%)", color="#E45756")
    axA2.tick_params(axis="y", labelcolor="#E45756")
    axA.set_title(f"SF{sf}: capacity ↓, overhead ↑ with $N$ (1 slot/node)")
    axA.grid(axis="y", linestyle=":", alpha=0.5)

    # Panel B — superframe duration vs N.
    axB.plot(ns, sframe, "-", color="#54A24B")
    axB.set_xlabel("Network size $N$ (nodes)")
    axB.set_ylabel("Superframe duration (s)")
    axB.set_title(f"SF{sf}: superframe duration grows with $N$")
    axB.grid(axis="y", linestyle=":", alpha=0.5)

    if n_meas is not None:
        for ax in (axA, axB):
            ax.axvline(n_meas, color="#888888", linestyle=":", linewidth=1.2)
        axA.annotate(f"measured\n$N={n_meas}$", (n_meas, cap[n_meas - 2]),
                     fontsize=8, ha="left", va="bottom",
                     xytext=(4, 4), textcoords="offset points")

    fig.suptitle(f"SF{sf} scalability projection (model only; "
                 f"anchored at measured $N={n_meas}$)", fontsize=12)
    fig.tight_layout()
    save_fig(fig, out_dir, "superframe_scalability_vs_n")


# ── CLI ──────────────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Combine measured anchors from several run batches into the "
                    "publication superframe / data-slot figure set.")
    ap.add_argument("--batch", action="append", type=Path, required=True,
                    metavar="DIR", help="A run-batch directory (repeatable).")
    ap.add_argument("--out", type=Path, required=True,
                    help="Output directory for the figures (created if absent).")
    ap.add_argument("--sf", type=int, default=9,
                    help="SF for the data-slot panels (composition vs ds, "
                         "sensitivity, validation). Default 9.")
    ap.add_argument("--drop-first", type=int, default=1,
                    help="Skip the first N reps per cell (cold start). Default 1.")
    ap.add_argument("--keep-partial", action="store_true",
                    help="Keep runs that did not reach full membership for their "
                         "SF (default: require full membership per SF).")
    ap.add_argument("--frame-selection", choices=("modal", "mean"), default="modal",
                    help="Per cell, report the modal converged frame (default) or "
                         "the mean over every run. Runs differing in hop depth "
                         "build different frames, so the mean describes no real "
                         "schedule.")
    args = ap.parse_args()

    anchors = aggregate_anchors(args.batch, drop_first=args.drop_first,
                                require_full_membership=not args.keep_partial,
                                frame_selection=args.frame_selection)
    if not anchors:
        raise SystemExit(f"no extractable superframe anchors under {args.batch}")

    print("aggregated anchors:")
    for (sf, ds) in sorted(anchors):
        a = anchors[(sf, ds)]
        print(f"  SF{sf} ds={ds}: n={a['n']} N={a['node_count']:.0f} "
              f"cap={a['capacity_Bps']:.2f}B/s ovh={a['overhead_frac']*100:.0f}% "
              f"recur={a['recurrence_s']:.1f}s pool={a['max_data_slots_cfg']}")

    _set_pub_style()
    fig_composition(anchors, args.out)
    fig_dataslots_composition(anchors, args.out, sf=args.sf)
    fig_pareto(anchors, args.out)
    fig_sensitivity(anchors, args.out, sf=args.sf)
    max_err = fig_validation(anchors, args.out, sf=args.sf)
    fig_scalability_vs_n(anchors, args.out, sf=args.sf)
    print(f"\nSF{args.sf} model-vs-measured max error: {max_err:.1f}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
