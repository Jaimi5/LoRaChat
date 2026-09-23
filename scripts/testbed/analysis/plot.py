"""Render a per-cell bar chart with 95% CI error bars from aggregated.json.

Default use: compare network convergence time across SF7/SF9/SF12 cells of the
`formation` batch, dropping the first cold-start repetition of each cell.

    python3 scripts/testbed/analysis/plot.py scripts/testbed/runs/formation
    python3 scripts/testbed/analysis/plot.py scripts/testbed/runs/formation --drop-first 1

If aggregated.json is missing (or `--drop-first` differs from the value stored
in it), this script regenerates it via multirun.aggregate() and writes it back
before plotting.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path

if __name__ == "__main__":
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.multirun import aggregate
from analysis.plot_paper import FIG_WIDE, _set_pub_style, save_fig
from analysis.plot_superframe import aggregate_measured


_SF_CELL_RE = re.compile(r"^SF(\d+)$")

_METRIC_LABELS = {
    "convergence_s": ("Network convergence (s)", "Convergence time"),
    "pdr": ("Packet delivery ratio", "PDR"),
    "latency_p50_ms": ("Latency p50 (ms)", "Latency p50"),
    "latency_p95_ms": ("Latency p95 (ms)", "Latency p95"),
    "latency_mean_ms": ("Latency mean (ms)", "Latency mean"),
    "offered_pkt_per_min": ("Offered load (pkt/min)", "Offered load"),
    "mean_current_mA": ("Mean current (mA)", "Mean current"),
}


def _load_or_regenerate(batch_dir: Path, drop_first: int | None) -> tuple[dict, str]:
    """Return (aggregated_data, status_string).

    Loads cached aggregated.json if it matches the requested drop_first;
    otherwise re-runs aggregate() and writes a fresh aggregated.json.
    """
    agg_path = batch_dir / "aggregated.json"
    if agg_path.is_file() and drop_first is None:
        return json.loads(agg_path.read_text()), f"loaded cached {agg_path.name}"

    if agg_path.is_file() and drop_first is not None:
        cached = json.loads(agg_path.read_text())
        if cached.get("drop_first", 0) == drop_first:
            return cached, f"loaded cached {agg_path.name} (drop_first={drop_first})"

    effective = drop_first or 0
    data = aggregate(batch_dir, drop_first=effective)
    agg_path.write_text(json.dumps(data, indent=2, sort_keys=True, default=list))
    return data, f"regenerated {agg_path.name} (drop_first={effective})"


def _sf_sorted_cells(cells: dict) -> list[tuple[str, int]]:
    """Return [(cell_id, sf_int), ...] sorted by SF number. Non-SF cells get sf=-1
    and are placed first (so they're still visible but obviously distinct)."""
    out: list[tuple[str, int]] = []
    for cell_id in cells:
        m = _SF_CELL_RE.match(cell_id)
        out.append((cell_id, int(m.group(1)) if m else -1))
    out.sort(key=lambda x: (x[1] == -1, x[1], x[0]))
    return out


def _bar_value_and_error(cell_data: dict, metric: str) -> tuple[float | None, float | None, int]:
    """Pull (mean, ci_half_width, n) for a metric from one cell's dict.
    Returns (None, None, 0) if the cell has no data for that metric.

    `n` is the metric's *own* sample count, not the cell's `n_runs`: a run that
    ran fine can still yield no value for a given metric (e.g. convergence is
    None when a run never reached full routing), so the two differ. Reporting
    n_runs would overstate the sample behind the bar.
    """
    n_runs = cell_data.get("n_runs", 0)
    if not n_runs:
        return None, None, 0
    m = cell_data.get(metric)
    if not isinstance(m, dict) or m.get("mean") is None:
        return None, None, 0
    mean = m["mean"]
    n = m.get("n", n_runs)
    ci = m.get("ci95")
    if ci and len(ci) == 2 and ci[0] is not None and ci[1] is not None:
        half = (ci[1] - ci[0]) / 2.0
    else:
        half = None
    return mean, half, n


def _per_run_values(data: dict, cell_id: str, metric: str) -> list[float]:
    """Per-run values of `metric` for one cell, from the aggregate's
    per_run_points rows. Runs with no value for the metric are skipped, so the
    list length matches the metric's own n. Empty if the aggregate predates
    per_run_points or the metric isn't carried there."""
    out: list[float] = []
    for row in data.get("per_run_points") or []:
        if row.get("cell") != cell_id:
            continue
        v = row.get(metric)
        if isinstance(v, (int, float)):
            out.append(float(v))
    return out


def _model_floors(batch_dir: Path, drop_first: int,
                  couple_depth: bool = True) -> dict[int, dict]:
    """Per-SF join-admission convergence floor from the measured superframe anchors.

    For each SF, aggregate the converged superframe structure measured across the
    batch (superframe.extract) and compute two floors from the firmware's
    join-admission cap (kMaxPendingJoins): the firmware best case k=3 and a
    hypothetical serialized worst case k=1. Each floor is the number of
    superframes needed to admit all N-1 non-root nodes, times the converged
    superframe duration:

        sf_count(k) = max(D, ceil((N-1)/k))   if couple_depth else ceil((N-1)/k)
        floor(k)    = sf_count(k) * superframe_s

    The same arithmetic lives in model.convergence_floor_s (which rebuilds the
    superframe from first principles); here we anchor on the *measured* converged
    superframe duration instead, so the floor sits on real data.
    Returns {sf: {superframe_s, max_hops, node_count, sf_count_3, sf_count_1,
    floor_3, floor_1}}.
    """
    anchors = aggregate_measured(batch_dir, drop_first=drop_first)
    out: dict[int, dict] = {}
    for sf, m in anchors.items():
        superframe_s = m["superframe_s"]
        max_hops = int(round(m["max_hops"]))
        node_count = int(round(m["node_count"]))
        join_sf = {k: math.ceil(max(node_count - 1, 0) / k) for k in (3, 1)}
        sf_count = {k: max(max_hops, n) if couple_depth else n
                    for k, n in join_sf.items()}
        out[sf] = {"superframe_s": superframe_s, "max_hops": max_hops,
                   "node_count": node_count,
                   "sf_count_3": sf_count[3], "sf_count_1": sf_count[1],
                   "floor_3": sf_count[3] * superframe_s,
                   "floor_1": sf_count[1] * superframe_s}
    return out


def _print_validation_table(cells: dict, floors: dict[int, dict],
                            couple_depth: bool) -> None:
    """Compare measured convergence against the two join-admission floors, per SF."""
    formula = ("max(D, ceil((N-1)/k)) x superframe" if couple_depth
               else "ceil((N-1)/k) x superframe")
    print(f"\nConvergence vs join-admission floor (floor = {formula}):")
    print(f"  {'SF':<4} {'measured(s)':>11} {'N':>3} {'D':>3} {'sframe_s':>9} "
          f"{'k=3 #SF':>8} {'floor3(s)':>10} {'meas/f3':>8} "
          f"{'k=1 #SF':>8} {'floor1(s)':>10} {'meas/f1':>8}")
    d_binds = False
    for cell_id, sf in _sf_sorted_cells(cells):
        if sf < 0 or sf not in floors:
            continue
        f = floors[sf]
        mean, _half, _n = _bar_value_and_error(cells[cell_id], "convergence_s")
        meas = f"{mean:11.1f}" if mean is not None else f"{'n/a':>11}"
        r3 = f"{mean / f['floor_3']:8.2f}" if mean is not None else f"{'--':>8}"
        r1 = f"{mean / f['floor_1']:8.2f}" if mean is not None else f"{'--':>8}"
        join3 = math.ceil(max(f["node_count"] - 1, 0) / 3)
        if couple_depth and f["max_hops"] > join3:
            d_binds = True
        print(f"  SF{sf:<2} {meas} {f['node_count']:>3} {f['max_hops']:>3} "
              f"{f['superframe_s']:9.1f} {f['sf_count_3']:>8} {f['floor_3']:10.1f} "
              f"{r3} {f['sf_count_1']:>8} {f['floor_1']:10.1f} {r1}")
    note = ("D is binding for >=1 SF (max picks depth)." if d_binds
            else "D is slack everywhere (join term dominates).")
    print(f"  (k = joins/superframe; k=3 firmware best, k=1 hypothetical serialized. "
          f"meas/floor >= 1 expected. {note})\n")


def _render_with_floor(plt, ordered, cells, floors, y_label, friendly,
                       batch_name, couple_depth: bool) -> None:
    """Grouped measured-vs-join-admission-floor bars for the convergence metric.

    Every SF group shows three bars, left→right: measured convergence (95% CI),
    the k=3 floor (firmware best case), and the k=1 floor (hypothetical
    serialized worst case). Bars are grayscale-safe (fill weight + hatch + black
    outline) and each floor is annotated with its superframe-count multiplier so
    ceil((N-1)/k) is legible without the formula. SF cells with no measured value
    (e.g. SF7, where the remote cluster's join across the 3.5 km link is intermittent
    because the reverse path's SNR sits at the SF7 decode threshold) draw the two
    floor bars only, annotated.
    """
    sf_cells = [(cid, sf) for cid, sf in ordered if sf >= 0 and sf in floors]
    if not sf_cells:
        raise SystemExit("no SF cells with model floors to plot")

    fig, ax = plt.subplots(figsize=(max(8.5, 2.2 * len(sf_cells) + 2), 5.4))
    width = 0.27
    xs = list(range(len(sf_cells)))
    meas_handle = f3_handle = f1_handle = None
    for i, (cell_id, sf) in enumerate(sf_cells):
        f = floors[sf]
        floor3, floor1 = f["floor_3"], f["floor_1"]
        mean, half, n = _bar_value_and_error(cells[cell_id], "convergence_s")

        # k=1 (worst, tallest) — red cross-hatch; k=3 (best) — green diagonal.
        b1 = ax.bar(i + width, floor1, width, color="#F2B6B5",
                    edgecolor="black", linewidth=0.7, hatch="xxx",
                    label="floor — 1 join/superframe (hypothetical serialized worst case)"
                    if f1_handle is None else None)
        f1_handle = f1_handle or b1
        b3 = ax.bar(i, floor3, width, color="#9FD89A",
                    edgecolor="black", linewidth=0.7, hatch="///",
                    label="floor — 3 joins/superframe (firmware best case)"
                    if f3_handle is None else None)
        f3_handle = f3_handle or b3
        ax.annotate(f"×{f['sf_count_3']} SF", (i, floor3), textcoords="offset points",
                    xytext=(0, 3), ha="center", va="bottom", fontsize=8.5)
        ax.annotate(f"×{f['sf_count_1']} SF", (i + width, floor1),
                    textcoords="offset points", xytext=(0, 3), ha="center",
                    va="bottom", fontsize=8.5)

        if mean is not None:
            mb = ax.bar(i - width, mean, width, yerr=half or 0.0, capsize=7,
                        color="#3B3B3B", edgecolor="black", linewidth=0.7,
                        error_kw={"elinewidth": 1.2, "ecolor": "#222"},
                        label="measured (95% CI)" if meas_handle is None else None)
            meas_handle = meas_handle or mb
            top = mean + (half or 0.0)
            ax.annotate(f"n={n}", (i - width, top), textcoords="offset points",
                        xytext=(0, 3), ha="center", va="bottom", fontsize=9)
            ax.annotate(f"{mean / floor3:.1f}× floor", (i - width, top),
                        textcoords="offset points", xytext=(0, 17), ha="center",
                        va="bottom", fontsize=9, fontweight="bold")
        else:
            ax.text(i - width, max(floor3, floor1) * 0.04,
                    "measured:\nremote join\nintermittent\n(reverse SNR at\nSF7 threshold) ✗",
                    ha="center", va="bottom", fontsize=8.5, color="#B22222")

    ax.set_xticks(xs)
    ax.set_xticklabels([cid for cid, _ in sf_cells])
    ax.set_xlabel("Spreading Factor")
    ax.set_ylabel(y_label)
    formula = ("max(D, ⌈(N−1)/k⌉) × superframe" if couple_depth
               else "⌈(N−1)/k⌉ × superframe")
    ax.set_title(f"{batch_name}: {friendly} — measured vs join-admission floor\n"
                 f"floor = {formula}   "
                 f"(k = nodes admitted per superframe; optimistic lower bound)",
                 fontsize=11)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    # Legend in reading order: measured, then best-case floor, then worst-case.
    handles = [h for h in (meas_handle, f3_handle, f1_handle) if h is not None]
    ax.legend(handles=handles, loc="upper left", framealpha=0.9, fontsize=8.5)
    # Headroom so the ratio labels and SF7 annotation don't clip the top.
    ax.margins(y=0.18)
    fig.tight_layout()


def render(batch_dir: Path, metric: str, drop_first: int | None,
           output: Path | None, with_model: bool = False,
           couple_depth: bool = True, paper: bool = False,
           title: str | None = None) -> Path:
    import matplotlib
    matplotlib.use("Agg")
    if paper:
        _set_pub_style()
    import matplotlib.pyplot as plt

    data, status = _load_or_regenerate(batch_dir, drop_first)
    print(status)

    cells = data.get("cells") or {}
    if not cells:
        raise SystemExit(f"no cells in {batch_dir}/aggregated.json")

    if metric not in _METRIC_LABELS:
        print(f"warning: unknown metric '{metric}', using raw label", file=sys.stderr)
    y_label, friendly = _METRIC_LABELS.get(metric, (metric, metric))

    ordered = _sf_sorted_cells(cells)
    batch_name = Path(data.get("batch_dir", batch_dir)).name or batch_dir.name
    df = data.get("drop_first", 0)

    model_mode = with_model and metric == "convergence_s"
    if model_mode:
        floors = _model_floors(batch_dir, df, couple_depth=couple_depth)
        _print_validation_table(cells, floors, couple_depth)
        _render_with_floor(plt, ordered, cells, floors, y_label, friendly,
                           batch_name, couple_depth)
        out = output or (batch_dir / f"{batch_name}_sf_comparison.png")
        plt.savefig(out, dpi=150)
        plt.close()
        print(f"wrote {out}")
        return out

    # Cells with no value for this metric (e.g. SF7 convergence, which never
    # completed because the long link was down) are dropped from the chart rather
    # than annotated: the plotter cannot know the *cause*, so the caption/prose
    # carries it. Dropped cells are logged to stderr so the omission is never
    # silent.
    labels: list[str] = []
    skipped: list[str] = []
    for cell_id, _sf in ordered:
        if _bar_value_and_error(cells[cell_id], metric)[0] is None:
            skipped.append(f"{cell_id} (0/{cells[cell_id].get('n_runs', 0)} runs)")
        else:
            labels.append(cell_id)
    if skipped:
        print(f"skipped cells with no {metric}: {', '.join(skipped)}",
              file=sys.stderr)
    if not labels:
        raise SystemExit(f"no cells have data for metric '{metric}'")

    figsize = FIG_WIDE if paper else (max(6.4, 1.5 * len(labels) + 2), 4.8)
    fig, ax = plt.subplots(figsize=figsize)
    x = list(range(len(labels)))
    points_seen = False
    data_max = 0.0

    for i, cell_id in enumerate(labels):
        mean, half, n = _bar_value_and_error(cells[cell_id], metric)
        ax.bar(i, mean, 0.5, yerr=half or 0.0, capsize=5 if paper else 8,
               color="#4C78A8", edgecolor="black", linewidth=0.6,
               error_kw={"elinewidth": 1.1, "ecolor": "#222"},
               zorder=2)

        # Raw per-run values over the bar: the mean+CI alone hides the spread.
        pts = _per_run_values(data, cell_id, metric)
        if pts:
            points_seen = True
            span = 0.16
            offs = ([0.0] if len(pts) == 1 else
                    [-span + 2 * span * k / (len(pts) - 1) for k in range(len(pts))])
            ax.scatter([i + o for o in offs], pts, s=12 if paper else 18,
                       facecolors="none", edgecolors="#1B3A5C",
                       linewidths=0.8, zorder=3)

        top = max([mean + (half or 0.0)] + pts)
        data_max = max(data_max, top)
        ax.annotate(f"n={n}", (i, top), xytext=(0, 3),
                    textcoords="offset points", ha="center", va="bottom",
                    fontsize=8 if paper else 9)

    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_xlim(-0.6, len(labels) - 0.4)
    # Explicit top headroom for the n= labels. margins() can't do this: set_ylim
    # fixes the y-axis and disables the autoscaling margins() would adjust.
    ax.set_ylim(0, data_max * (1.18 if paper else 1.12))
    ax.set_xlabel("Spreading Factor")
    ax.set_ylabel(y_label)

    if paper:
        # No batch prefix; single line. `--title ""` omits it (caption carries it).
        heading = friendly if title is None else title
        if heading:
            ax.set_title(heading)
    else:
        subtitle = "mean, 95% CI" + (", individual runs" if points_seen else "")
        ax.set_title(f"{batch_name}: {friendly} per SF\n{subtitle}", fontsize=11)
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    fig.tight_layout()

    if paper:
        out = output or (batch_dir / f"{batch_name}_sf_comparison.pdf")
        save_fig(fig, out.parent, out.stem)
        return out.parent / f"{out.stem}.pdf"

    out = output or (batch_dir / f"{batch_name}_sf_comparison.png")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Plot per-SF metric with 95% CI")
    ap.add_argument("batch_dir", type=Path,
                    help="e.g. scripts/testbed/runs/formation")
    ap.add_argument("--metric", default="convergence_s",
                    help=f"Metric key in aggregated.json (default: convergence_s). "
                         f"Known: {', '.join(_METRIC_LABELS)}")
    ap.add_argument("--drop-first", type=int, default=None, metavar="N",
                    help="If set, regenerate aggregated.json with this many leading "
                         "reps dropped per cell (default: use cached file as-is)")
    ap.add_argument("-o", "--output", type=Path,
                    help="Output PNG path (default: <batch_dir>/<batch>_sf_comparison.png)")
    ap.add_argument("--model", action="store_true",
                    help="Overlay the join-admission convergence floors (k=3 firmware "
                         "best case, k=1 hypothetical serialized) beside each measured "
                         "bar and print a measured-vs-floor table. convergence_s only.")
    ap.add_argument("--floor-formula", choices=("maxd", "join"), default="maxd",
                    help="Floor formula: 'maxd' = max(D, ceil((N-1)/k))×superframe "
                         "(default, subsumes the depth floor); 'join' = "
                         "ceil((N-1)/k)×superframe (pure join, ignores depth).")
    ap.add_argument("--paper", action="store_true",
                    help="Publication styling: serif/embedded fonts, compact "
                         "single-column size, no batch prefix in the title, and "
                         "write PDF+SVG+PNG. Plain (non-model) path only.")
    ap.add_argument("--title", default=None,
                    help="Override the figure title (--paper only). Pass an empty "
                         "string to omit the title so the caption carries it.")
    args = ap.parse_args()
    render(args.batch_dir, args.metric, args.drop_first, args.output, args.model,
           couple_depth=(args.floor_formula == "maxd"),
           paper=args.paper, title=args.title)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
