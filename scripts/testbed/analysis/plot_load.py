"""Plot PDR (and latency) versus normalized offered load, per spreading factor.

The raw PDR-vs-SF bar chart (plot.py) is load-confounded: it holds offered load
fixed while the TDMA capacity collapses with SF, so a load-saturation artifact
looks like a protocol property. This view instead puts the dimensionless load on
the x-axis — `rho = offered / capacity` (per-node) or the bottleneck-relay
utilization `rho_max` — so SF7/9/12 fall on one degradation curve and a cell that
saturates simply sits past rho = 1.

    python3 scripts/testbed/analysis/plot_load.py scripts/testbed/runs/main_cluster_pdr
    python3 scripts/testbed/analysis/plot_load.py <batch_dir> --rho node

Per-run points come from aggregated.json's `per_run_points` (multirun.aggregate);
the file is regenerated if missing or lacking that field.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

if __name__ == "__main__":
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.multirun import aggregate

# Grayscale-safe, SF-keyed marker styles.
_SF_STYLE = {
    7:  {"color": "#4C78A8", "marker": "o", "label": "SF7"},
    9:  {"color": "#F58518", "marker": "s", "label": "SF9"},
    12: {"color": "#E45756", "marker": "^", "label": "SF12"},
}


def _load_points(batch_dir: Path, drop_first: int) -> list[dict]:
    agg_path = batch_dir / "aggregated.json"
    data = None
    if agg_path.is_file():
        data = json.loads(agg_path.read_text())
        if "per_run_points" not in data or data.get("drop_first", 0) != drop_first:
            data = None
    if data is None:
        data = aggregate(batch_dir, drop_first=drop_first)
        agg_path.write_text(json.dumps(data, indent=2, sort_keys=True, default=list))
    return data.get("per_run_points", [])


def render(batch_dir: Path, rho_key: str, drop_first: int, output: Path | None) -> Path:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    points = [p for p in _load_points(batch_dir, drop_first)
              if p.get(rho_key) is not None and p.get("pdr") is not None]
    if not points:
        raise SystemExit(f"no per-run points with '{rho_key}' and pdr in {batch_dir}")

    batch_name = batch_dir.name
    x_label = ("Normalized offered load  ρ = offered / capacity (per node)"
               if rho_key == "rho_node"
               else "Bottleneck-relay utilization  ρ_max")

    fig, (ax_pdr, ax_lat) = plt.subplots(2, 1, figsize=(8.0, 7.6), sharex=True)

    seen_sf = set()
    for p in points:
        sf = p.get("sf")
        st = _SF_STYLE.get(sf, {"color": "#888", "marker": "x", "label": f"SF{sf}"})
        lbl = st["label"] if sf not in seen_sf else None
        seen_sf.add(sf)
        ax_pdr.scatter(p[rho_key], p["pdr"], c=st["color"], marker=st["marker"],
                       s=70, edgecolor="black", linewidth=0.5, label=lbl, zorder=3)
        lat = p.get("latency_p50_ms")
        if lat is not None:
            ax_lat.scatter(p[rho_key], lat / 1000.0, c=st["color"], marker=st["marker"],
                           s=70, edgecolor="black", linewidth=0.5, zorder=3)

    for ax in (ax_pdr, ax_lat):
        ax.axvline(1.0, color="#B22222", linestyle="--", linewidth=1.2, zorder=1)
        ax.grid(True, linestyle=":", alpha=0.5)
        ax.set_axisbelow(True)
    ax_pdr.text(1.02, 0.05, "ρ = 1\n(saturation)", color="#B22222", fontsize=8.5,
                transform=ax_pdr.get_xaxis_transform(), va="bottom")
    ax_pdr.axhline(0.95, color="#2a7", linestyle=":", linewidth=1.0, zorder=1)

    ax_pdr.set_ylabel("Packet delivery ratio")
    ax_pdr.set_ylim(0, 1.05)
    ax_pdr.set_title(f"{batch_name}: PDR and latency vs offered load (per SF)")
    ax_pdr.legend(loc="lower left", framealpha=0.9, fontsize=9)
    ax_lat.set_ylabel("Latency p50 (s)")
    ax_lat.set_yscale("log")
    ax_lat.set_xlabel(x_label)
    fig.tight_layout()

    out = output or (batch_dir / f"{batch_name}_load_curve.png")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Plot PDR/latency vs normalized offered load per SF")
    ap.add_argument("batch_dir", type=Path, help="e.g. scripts/testbed/runs/main_cluster_pdr")
    ap.add_argument("--rho", choices=("node", "max"), default="max",
                    help="x-axis: 'max' = bottleneck-relay utilization (default), "
                         "'node' = per-node offered/capacity")
    ap.add_argument("--drop-first", type=int, default=1, metavar="N",
                    help="Drop the first N reps per cell when (re)aggregating (default 1)")
    ap.add_argument("-o", "--output", type=Path, help="Output PNG path")
    args = ap.parse_args()
    rho_key = "rho_node" if args.rho == "node" else "rho_max"
    render(args.batch_dir, rho_key, args.drop_first, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
