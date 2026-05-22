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
import re
import sys
from pathlib import Path

if __name__ == "__main__":
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.multirun import aggregate


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
    """Pull (mean, ci_half_width, n_runs) for a metric from one cell's dict.
    Returns (None, None, 0) if the cell has no data for that metric."""
    n = cell_data.get("n_runs", 0)
    if not n:
        return None, None, 0
    m = cell_data.get(metric)
    if not isinstance(m, dict) or m.get("mean") is None:
        return None, None, n
    mean = m["mean"]
    ci = m.get("ci95")
    if ci and len(ci) == 2 and ci[0] is not None and ci[1] is not None:
        half = (ci[1] - ci[0]) / 2.0
    else:
        half = None
    return mean, half, n


def render(batch_dir: Path, metric: str, drop_first: int | None,
           output: Path | None) -> Path:
    import matplotlib
    matplotlib.use("Agg")
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
    labels: list[str] = []
    means: list[float] = []
    errs: list[float] = []
    ns: list[int] = []
    for cell_id, _sf in ordered:
        mean, half, n = _bar_value_and_error(cells[cell_id], metric)
        if mean is None:
            continue
        labels.append(cell_id)
        means.append(mean)
        errs.append(half if half is not None else 0.0)
        ns.append(n)

    if not means:
        raise SystemExit(f"no cells have data for metric '{metric}'")

    fig, ax = plt.subplots(figsize=(max(7.5, 1.4 * len(labels) + 2), 4.8))
    x = list(range(len(labels)))
    bars = ax.bar(x, means, yerr=errs, capsize=8, color="#4C78A8",
                  edgecolor="black", linewidth=0.6,
                  error_kw={"elinewidth": 1.2, "ecolor": "#222"})

    for i, bar in enumerate(bars):
        height = bar.get_height()
        y = height + (errs[i] if errs[i] else 0)
        ax.text(bar.get_x() + bar.get_width() / 2.0, y,
                f"n={ns[i]}", ha="center", va="bottom", fontsize=9)

    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_xlabel("Spreading Factor")
    ax.set_ylabel(y_label)

    batch_name = Path(data.get("batch_dir", batch_dir)).name or batch_dir.name
    df = data.get("drop_first", 0)
    suffix = f" (dropped first {df} rep/cell)" if df else ""
    ax.set_title(f"{batch_name}: {friendly} per SF — 95% CI{suffix}")
    ax.grid(axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    fig.tight_layout()

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
    args = ap.parse_args()
    render(args.batch_dir, args.metric, args.drop_first, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
