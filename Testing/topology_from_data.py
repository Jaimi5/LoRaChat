"""
topology_from_data.py — LoRa Mesh Topology Visualizer from RT Snapshot Data

Usage:
    python Testing/topology_from_data.py Testing/data1.json

Reads a JSON array of RT snapshot messages collected via MQTT and renders a
directed graph showing asymmetric link quality between nodes.
"""

import json
import math
import os
import statistics
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

import matplotlib.pyplot as plt
import matplotlib.cm as cm
import matplotlib.colors as mcolors
import networkx as nx
import numpy as np


# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------

@dataclass
class EdgeStats:
    avg: float
    std: float
    min_lq: int
    max_lq: int
    count: int


# ---------------------------------------------------------------------------
# 1. Load
# ---------------------------------------------------------------------------

def load_rt_snapshots(filepath: str) -> List[dict]:
    """Return the list of RT snapshot dicts from a JSON file."""
    with open(filepath, "r") as f:
        data = json.load(f)
    snapshots = []
    for entry in data:
        try:
            rt = entry["payload"]["RT"]
            snapshots.append(rt)
        except (KeyError, TypeError):
            pass  # skip malformed entries
    return snapshots


# ---------------------------------------------------------------------------
# 2. Aggregate link quality per directed edge (hop_count == 1 only)
# ---------------------------------------------------------------------------

def aggregate_link_quality(snapshots: List[dict]) -> Dict[Tuple[int, int], List[int]]:
    """
    Returns a mapping (src, dst) -> [link_quality, ...] using only rows where
    hop_count == 1 (direct neighbors, not routed paths).
    """
    lq_map: Dict[Tuple[int, int], List[int]] = {}
    for rt in snapshots:
        src = rt.get("addrSrc")
        if src is None:
            continue
        for row in rt.get("rt", []):
            if row.get("hop_count") != 1:
                continue
            dst = row.get("neighbor")
            lq = row.get("link_quality")
            if dst is None or lq is None:
                continue
            key = (src, dst)
            lq_map.setdefault(key, []).append(lq)
    return lq_map


# ---------------------------------------------------------------------------
# 3. Compute statistics per directed edge
# ---------------------------------------------------------------------------

def compute_edge_stats(lq_map: Dict[Tuple[int, int], List[int]]) -> Dict[Tuple[int, int], EdgeStats]:
    stats: Dict[Tuple[int, int], EdgeStats] = {}
    for (src, dst), values in lq_map.items():
        avg = statistics.mean(values)
        std = statistics.stdev(values) if len(values) > 1 else 0.0
        stats[(src, dst)] = EdgeStats(
            avg=avg,
            std=std,
            min_lq=min(values),
            max_lq=max(values),
            count=len(values),
        )
    return stats


# ---------------------------------------------------------------------------
# 4. Build directed graph
# ---------------------------------------------------------------------------

def build_digraph(edge_stats: Dict[Tuple[int, int], EdgeStats]) -> nx.DiGraph:
    G = nx.DiGraph()
    for (src, dst), s in edge_stats.items():
        G.add_edge(src, dst, avg=s.avg, std=s.std, min_lq=s.min_lq,
                   max_lq=s.max_lq, count=s.count)
    return G


# ---------------------------------------------------------------------------
# 5. Layout
# ---------------------------------------------------------------------------

def compute_layout(G: nx.DiGraph) -> Dict[int, np.ndarray]:
    if len(G.nodes) <= 6:
        return nx.circular_layout(G)
    return nx.spring_layout(G, seed=42)


# ---------------------------------------------------------------------------
# 6. Arc label position helper
# ---------------------------------------------------------------------------

def get_arc_label_pos(
    pos_u: np.ndarray,
    pos_v: np.ndarray,
    rad: float,
    offset_frac: float = 0.55,
) -> Tuple[float, float]:
    """
    Return (x, y) for a label on the bowed arc from u to v.

    The label is placed at the chord midpoint offset by `rad` times a
    CCW-perpendicular unit vector scaled by the chord length and offset_frac.
    Because the forward and reverse arcs travel in opposite directions their
    CCW perpendiculars point to opposite sides of the chord automatically.
    """
    mid = (pos_u + pos_v) / 2.0
    chord = pos_v - pos_u
    chord_len = np.linalg.norm(chord)
    if chord_len < 1e-9:
        return float(mid[0]), float(mid[1])
    # CCW perpendicular of (dx, dy) is (-dy, dx)
    perp = np.array([-chord[1], chord[0]]) / chord_len
    label_pos = mid + rad * perp * chord_len * offset_frac
    return float(label_pos[0]), float(label_pos[1])


# ---------------------------------------------------------------------------
# 7. Draw topology
# ---------------------------------------------------------------------------

LQ_VMIN = 200
LQ_VMAX = 255
COLORMAP = "RdYlGn"
ARC_RAD = 0.25


def draw_topology(
    G: nx.DiGraph,
    edge_stats: Dict[Tuple[int, int], EdgeStats],
    pos: Dict[int, np.ndarray],
    title: str,
) -> plt.Figure:
    cmap = plt.colormaps[COLORMAP]
    norm = mcolors.Normalize(vmin=LQ_VMIN, vmax=LQ_VMAX)

    fig, ax = plt.subplots(figsize=(10, 8))
    ax.set_title(title, fontsize=13, fontweight="bold")
    ax.axis("off")

    # --- nodes ---
    nx.draw_networkx_nodes(G, pos, ax=ax, node_size=1400,
                           node_color="#4A90D9", alpha=0.9)
    nx.draw_networkx_labels(G, pos, ax=ax,
                            labels={n: str(n) for n in G.nodes()},
                            font_size=8, font_color="white", font_weight="bold")

    # --- edges (one at a time for per-edge color) ---
    for src, dst in G.edges():
        s = edge_stats[(src, dst)]
        color = cmap(norm(s.avg))

        nx.draw_networkx_edges(
            G, pos, ax=ax,
            edgelist=[(src, dst)],
            edge_color=[color],
            connectionstyle=f"arc3,rad={ARC_RAD}",
            arrows=True,
            arrowsize=18,
            width=2.0,
            min_source_margin=30,
            min_target_margin=30,
        )

        # --- edge label ---
        p_src = np.array(pos[src])
        p_dst = np.array(pos[dst])
        lx, ly = get_arc_label_pos(p_src, p_dst, ARC_RAD)

        if s.std >= 0.1:
            label = f"{s.avg:.0f}±{s.std:.1f}"
        else:
            label = f"{s.avg:.0f}"

        ax.text(
            lx, ly, label,
            fontsize=7,
            ha="center", va="center",
            color=_darken(color),
            bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="none", alpha=0.85),
            zorder=5,
        )

    # --- colorbar ---
    sm = cm.ScalarMappable(cmap=cmap, norm=norm)
    sm.set_array([])
    cbar = fig.colorbar(sm, ax=ax, shrink=0.6, pad=0.02)
    cbar.set_label("Link quality (avg)", fontsize=10)

    plt.tight_layout()
    return fig


def _darken(rgba, factor: float = 0.65) -> Tuple[float, float, float, float]:
    """Return a darkened version of an RGBA color for readable label text."""
    r, g, b, a = rgba
    return (r * factor, g * factor, b * factor, a)


# ---------------------------------------------------------------------------
# 8. Console summary table
# ---------------------------------------------------------------------------

def print_summary_table(
    edge_stats: Dict[Tuple[int, int], EdgeStats],
    n_snapshots: int,
) -> None:
    print(f"\nRT Snapshot Summary  ({n_snapshots} snapshots parsed)\n")
    header = f"{'Src':>7}  {'Dst':>7}  {'Avg LQ':>7}  {'Std':>6}  {'Min':>5}  {'Max':>5}  {'Count':>6}"
    print(header)
    print("-" * len(header))
    for (src, dst), s in sorted(edge_stats.items()):
        std_str = f"{s.std:.2f}" if s.std >= 0.01 else "  —  "
        print(
            f"{src:>7}  {dst:>7}  {s.avg:>7.1f}  {std_str:>6}  "
            f"{s.min_lq:>5}  {s.max_lq:>5}  {s.count:>6}"
        )
    print()


# ---------------------------------------------------------------------------
# 9. main
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <data_file.json>")
        sys.exit(1)

    filepath = sys.argv[1]
    snapshots = load_rt_snapshots(filepath)
    if not snapshots:
        print(f"No RT snapshots found in {filepath}")
        sys.exit(1)

    lq_map = aggregate_link_quality(snapshots)
    edge_stats = compute_edge_stats(lq_map)

    print_summary_table(edge_stats, n_snapshots=len(snapshots))

    G = build_digraph(edge_stats)
    pos = compute_layout(G)

    title = f"LoRa Mesh Topology — {len(G.nodes())} nodes, {len(G.edges())} directed links\n({filepath})"
    fig = draw_topology(G, edge_stats, pos, title)

    out_path = os.path.splitext(os.path.abspath(filepath))[0] + "_topology.png"
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    print(f"Plot saved to: {out_path}")
    _open_file(out_path)


def _open_file(path: str) -> None:
    """Open a file with the default viewer, with WSL→Windows fallback."""
    try:
        if sys.platform == "win32":
            os.startfile(path)
        elif sys.platform == "darwin":
            subprocess.run(["open", path], check=True)
        else:
            # Native Linux: try xdg-open
            result = subprocess.run(
                ["xdg-open", path],
                stderr=subprocess.DEVNULL,
                timeout=3,
            )
            if result.returncode != 0:
                raise OSError
    except Exception:
        # WSL fallback: open through Windows Explorer
        try:
            win_path = subprocess.check_output(
                ["wslpath", "-w", path], text=True
            ).strip()
            subprocess.Popen(["explorer.exe", win_path])
        except Exception:
            pass  # nothing left to try; the file was already saved


if __name__ == "__main__":
    main()
