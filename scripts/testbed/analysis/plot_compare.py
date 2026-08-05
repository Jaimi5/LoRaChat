"""Overlay two campaigns (Previous LoRaMesher [v1] vs T-LoRaMesher [v2]) on one figure.

`plot.py` and `plot_load.py` each read a single batch dir; `plot_paper.py` only
handles superframe metrics. This script takes the two `aggregated.json` produced
by `multirun.py` for the paired `sim_load_compare` campaigns and renders the
energy comparison plus the version-neutral trade-off panels:

    python3 scripts/testbed/analysis/plot_compare.py \
        --v1 scripts/testbed/runs/sim_load_compare_prev \
        --v2 scripts/testbed/runs/sim_load_compare \
        --out docs/paper/figures/energy

Figures written to --out:
  energy_mean_current.png      mean radio current, v1 vs v2 (headline)
  energy_per_delivered_bit.png mJ per delivered app bit (equal-data normalizer)
  app_pdr.png                  fair application PDR = delivered / intended, intended =
                               packet_count × DESIGNED source count (un-joined sources
                               counted as undelivered load); invalid cells shaded
  sender_participation.png     fraction of designed sources that joined and sent — the
                               honesty companion showing WHY v1's PDR differs
  app_delivered_count.png      absolute packets delivered, v1 vs v2 ("v2 delivers more")
  app_latency_p50.png          p50 end-to-end latency (the v2 cost of TDMA)
  app_latency_p95.png          p95 end-to-end latency
  energy_sleep_crossover.png   model.py projection: v2 mean current vs sleep
                               depth (duty target), with the v1 floor overlaid

Cells are matched between campaigns by (SF, load-delay) parsed from the cell id
with multirun's own regexes, so only conditions present in both are plotted.
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

from analysis.multirun import _SF_OF_CELL, _DELAY_OF_CELL, _SIZE_OF_CELL
from analysis.model import metrics as model_metrics
from analysis.plot_paper import FIG_HALF, FIG_WIDE, _set_pub_style
from analysis.radio_power import (RX_BAND1_LNABOOST_ON_MA, RX_BAND23_MA,
                                  SOURCES)

# Categorical two-series palette (validated CVD-safe, blue↔orange ΔE 96.7, both
# ≥3:1 on a light surface — see the dataviz skill's validate_palette.js). v1 and v2
# are distinct ENTITIES, so they get distinct hues, not two shades of one hue.
_V1 = {"color": "#eb6834", "label": "Previous LoRaMesher (always-on RX)"}
_V2 = {"color": "#2a78d6", "label": "T-LoRaMesher (TDMA superframe)"}


def _apply_paper_style() -> None:
    """Clean, legible defaults for publication PNGs (idempotent).

    Typography, font embedding and canvas sizing come from plot_paper's shared
    publication style so these figures match the rest of the paper's figure set;
    only the spine trim is specific to this module.
    """
    import matplotlib
    _set_pub_style()
    matplotlib.rcParams.update({
        "axes.spines.top": False,
        "axes.spines.right": False,
    })


def _savefig_multi(fig, out: Path, **kw) -> None:
    """Write the figure at `out` (PNG) and a sibling vector `.pdf` for the paper."""
    fig.savefig(out, **kw)
    print(f"wrote {out}")
    if out.suffix.lower() != ".pdf":
        pdf = out.with_suffix(".pdf")
        fig.savefig(pdf, **kw)
        print(f"wrote {pdf}")


def _invalid_cells(keys, v1, v2) -> dict:
    """Map cell key → human reason for cells that must NOT read as a v1-vs-v2 result.

    Two failure modes, both independently verified in the campaigns:
      * BOTH versions deliver ~0 — payload exceeds the single-packet LoRa MTU and
        neither stack does app-layer fragmentation (v2 rejects at the sender, v1
        truncates on-air). A shared limitation, not a comparison.
      * A version's sources are mostly ABSENT from the capture — participation < 0.5
        means fewer than half the designed sources were recorded (e.g. sim_size_compare
        SF9_s40 v2: 3 of 10). Verified root cause there was a harness capture-window miss
        (the first cell's cold build stalled upload 44 min; senders ran their burst before
        the monitors reattached), NOT a protocol failure — but either way the cell can't be
        compared. This is DISTINCT from the honest full-topology charge (v1 remote cluster,
        participation ~0.77), which stays VALID and is disclosed by the participation panel.
    """
    reasons: dict = {}
    for k in keys:
        c1, c2 = v1.get(k, {}), v2.get(k, {})
        d1 = (c1.get("app_delivered") or {}).get("mean")
        d2 = (c2.get("app_delivered") or {}).get("mean")
        if (d1 is not None and d2 is not None and d1 < 1 and d2 < 1):
            reasons[k] = "payload > MTU\nboth fail"
            continue
        p1 = (c1.get("sender_participation") or {}).get("mean")
        p2 = (c2.get("sender_participation") or {}).get("mean")
        if (p1 is not None and p1 < 0.5) or (p2 is not None and p2 < 0.5):
            who = ("Prev. LoRaMesher" if (p1 is not None and p1 < 0.5)
                   else "T-LoRaMesher")
            reasons[k] = f"{who}\npartial capture"
    return reasons


def _load(run_dir: Path) -> dict:
    agg = run_dir / "aggregated.json"
    if not agg.is_file():
        raise SystemExit(f"missing {agg} — run multirun.py on {run_dir} first")
    return json.loads(agg.read_text())


def _cells_by_key(data: dict) -> dict[tuple[int, int, int], dict]:
    """Map (sf, delay_ms, size_B) → cell dict for every cell with usable runs.

    `size_B` distinguishes packet-size sweep cells (e.g. "SF9_s120") that share a
    fixed delay — without it a size sweep would collapse every size onto one
    (sf, delay) key. It defaults to 0 for load-sweep cells that carry no `_s`
    token, so the load comparison keying is unchanged.
    """
    out: dict[tuple[int, int, int], dict] = {}
    for cell, c in data.get("cells", {}).items():
        if not isinstance(c, dict) or c.get("n_runs", 0) == 0:
            continue
        msf = _SF_OF_CELL.search(cell)
        if not msf:
            continue
        sf = int(msf.group(1))
        md = _DELAY_OF_CELL.search(cell)
        delay = int(md.group(1)) if md else 0
        ms = _SIZE_OF_CELL.search(cell)
        size = int(ms.group(1)) if ms else 0
        out[(sf, delay, size)] = c
    return out


def _mean_err(cell: dict, metric: str) -> tuple[float | None, float | None]:
    """Return (mean, ci95_halfwidth) for a metric; halfwidth None if n<2."""
    m = cell.get(metric)
    if not isinstance(m, dict) or m.get("mean") is None:
        return (None, None)
    mean = m["mean"]
    ci = m.get("ci95")
    half = (mean - ci[0]) if (isinstance(ci, (list, tuple)) and ci) else None
    return (mean, half)


def _xlabel(key: tuple[int, int, int], x: str = "load") -> str:
    sf, delay, size = key
    if x == "size":
        return f"SF{sf}\n{size} B" if size else f"SF{sf}"
    return f"SF{sf}\n{delay // 1000}s" if delay else f"SF{sf}"


def _grouped_bar(ax, keys, v1, v2, metric, ylabel, scale=1.0, logy=False, x="load",
                 invalid=None):
    """Draw paired v1/v2 bars for `metric` across the matched cell keys.

    `invalid` (key → reason) shades those cells grey with a hatch and a label so a
    zero/stall cell can never be misread as a clean v1-vs-v2 outcome."""
    import numpy as np
    invalid = invalid or {}
    xs = np.arange(len(keys))
    w = 0.38
    # Shade invalid columns behind the bars first (so the marks sit on top).
    for i, k in enumerate(keys):
        if k in invalid:
            ax.axvspan(i - 0.5, i + 0.5, color="#e6e4df", alpha=0.7, zorder=0)
    # Per-bar numeric value labels: 2 decimals for a 0–1 ratio (PDR), whole
    # seconds for a latency metric. Requested in the paper's figure TODOs so a
    # reader gets the exact value without eyeballing the axis.
    is_pdr = "pdr" in metric
    fmt = (lambda v: f"{v:.2f}") if is_pdr else (lambda v: f"{v:.0f}")
    for off, src, st in ((-w / 2, v1, _V1), (w / 2, v2, _V2)):
        means, errs = [], []
        for k in keys:
            mu, half = _mean_err(src.get(k, {}), metric)
            means.append(mu * scale if mu is not None else np.nan)
            errs.append(half * scale if half is not None else 0.0)
        ax.bar(xs + off, means, w, yerr=errs, capsize=3, color=st["color"],
               edgecolor="black", linewidth=0.5, label=st["label"],
               error_kw={"linewidth": 1.0}, zorder=3)
        for xi, mu, er in zip(xs + off, means, errs):
            if mu is None or np.isnan(mu):
                continue
            top = mu + er  # sit above the upper error-bar cap (valid on log too)
            ax.annotate(fmt(mu), (xi, top), ha="center", va="bottom",
                        fontsize=7.5, color="#222", zorder=4,
                        xytext=(0, 3.5), textcoords="offset points")
    # Invalid-cell labels (drawn near the top of the axes).
    for i, k in enumerate(keys):
        if k in invalid:
            ax.annotate(invalid[k], (i, 0.97), xycoords=("data", "axes fraction"),
                        ha="center", va="top", fontsize=8, color="#555",
                        fontstyle="italic")
    ax.set_xticks(xs)
    ax.set_xticklabels([_xlabel(k, x) for k in keys])
    ax.set_ylabel(ylabel)
    if logy:
        ax.set_yscale("log")
    ax.grid(True, axis="y", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)
    ax.legend(framealpha=0.9)


def _bar_figure(keys, v1, v2, metric, ylabel, title, out, scale=1.0, logy=False,
                x="load", invalid=None, caption=None, figsize=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    _apply_paper_style()
    fig, ax = plt.subplots(figsize=figsize or FIG_WIDE)
    _grouped_bar(ax, keys, v1, v2, metric, ylabel, scale=scale, logy=logy, x=x,
                 invalid=invalid)
    # A title wider than the canvas would be kept by the tight bounding box and
    # inflate the saved figure past its preset size. Pass title=None where the
    # surrounding caption already names the figure.
    if title:
        ax.set_title(title)
    ax.set_xlabel("Spreading factor × payload size" if x == "size"
                  else "Spreading factor × send interval")
    if caption:
        # Reserve a bottom margin for a wrapped limitations footnote.
        fig.subplots_adjust(bottom=0.26)
        fig.text(0.5, 0.015, caption, ha="center", va="bottom", fontsize=7.5,
                 color="#555", wrap=True)
    else:
        fig.tight_layout()
    _savefig_multi(fig, out)
    plt.close(fig)


def _crossover_figure(v1, keys, out, nodes: int, max_hops: int,
                      source: str = "best", tx_power_dbm: float = 2.0):
    """v2 model mean current vs duty-cycle target (sleep depth), per SF, against
    each SF's modelled v1 floor. The duty at which the v2 curve drops below the
    v1 line is the sleep depth where TDMA wins.

    Both curves are drawn as a band spanning the plausible I_RX range rather than
    a single line. I_RX — not I_TX — is what decides this comparison: v1 is
    ~99.3% RX and v2 ~72.8%, so I_RX scales both while I_TX barely touches v1.
    The band's width is the honest uncertainty, and the crossover is an interval,
    not a point.
    """
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    from analysis.plot_load import _SF_STYLE

    sfs = sorted({k[0] for k in keys})
    duties = np.geomspace(0.005, 0.10, 40)

    # v1 floor per SF: mean across that SF's matched cells.
    v1_floor: dict[int, float] = {}
    for sf in sfs:
        vals = [v1[k]["mean_current_mA"]["mean"]
                for k in keys if k[0] == sf
                and isinstance(v1.get(k, {}).get("mean_current_mA"), dict)
                and v1[k]["mean_current_mA"].get("mean") is not None]
        if vals:
            v1_floor[sf] = float(np.mean(vals))

    # The v2 model's I_RX sensitivity, scaled onto its curve. Band 1 LnaBoost-on
    # (11.5 mA, our 869.525 MHz deployment) is the point estimate; the 433 MHz
    # bands-2&3 row (12.0 mA) is the high end, being what the literature source
    # actually measured.
    rx_lo, rx_hi = RX_BAND1_LNABOOST_ON_MA, RX_BAND23_MA

    fig, ax = plt.subplots(figsize=(7.8, 4.8))
    for sf in sfs:
        st = _SF_STYLE.get(sf, {"color": "#888", "marker": "x", "label": f"SF{sf}"})
        band = {}
        for tag, i_rx in (("lo", rx_lo), ("hi", rx_hi)):
            band[tag] = np.array([
                model_metrics(sf, node_count=nodes, data_slots=1, duty_cycle=d,
                              max_hops=max_hops, tx_power_dbm=tx_power_dbm,
                              source=source, i_rx_override=i_rx)["mean_current_mA"]
                for d in duties])
        ax.fill_between(duties, band["lo"], band["hi"], color=st["color"],
                        alpha=0.18, lw=0)
        ax.plot(duties, band["lo"], color=st["color"], lw=2,
                label=f"T-LoRaMesher {st['label']}")
        if sf in v1_floor:
            ax.axhline(v1_floor[sf], color=st["color"], ls="--", lw=1.2, alpha=0.8)
            # Crossover is an interval: the optimistic and pessimistic I_RX give
            # different duties. Shade between them rather than claiming a point.
            xs = []
            for tag in ("lo", "hi"):
                below = [d for d, c in zip(duties, band[tag]) if c < v1_floor[sf]]
                if below:
                    xs.append(max(below))
            if len(xs) == 2 and xs[0] != xs[1]:
                ax.axvspan(min(xs), max(xs), color=st["color"], alpha=0.10, lw=0)
            for dc in xs:
                ax.scatter([dc], [v1_floor[sf]], color=st["color"], s=45,
                           zorder=5, edgecolor="black", linewidth=0.5)

    ax.axvline(0.10, color="#444", ls=":", lw=1.0)
    ax.text(0.10, ax.get_ylim()[1] * 0.96, " testbed duty = 10%",
            fontsize=8.5, va="top", color="#444")
    ax.set_xscale("log")
    ax.set_xlabel("T-LoRaMesher TDMA duty-cycle target  (lower = deeper sleep)")
    ax.set_ylabel("Mean radio current (mA)")
    ax.set_title("Where TDMA sleep beats always-on RX", fontsize=12)
    ax.grid(True, which="both", linestyle=":", alpha=0.5)
    ax.set_axisbelow(True)

    # The dashed floors and the band carry meaning but have no natural legend
    # entry, so name them explicitly rather than leaving them to the caption.
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch
    handles, labels = ax.get_legend_handles_labels()
    handles += [
        Line2D([], [], color="#555", ls="--", lw=1.2,
               label="Previous LoRaMesher floor (model)"),
        Patch(facecolor="#555", alpha=0.18,
              label=f"$I_{{RX}}$ {rx_lo:g}–{rx_hi:g} mA uncertainty"),
    ]
    ax.legend(handles=handles, fontsize=8.5, framealpha=0.9, loc="upper left")
    fig.tight_layout()
    _savefig_multi(fig, out, dpi=150)
    plt.close(fig)


def render(v1_dir: Path, v2_dir: Path, out_dir: Path,
           nodes: int, max_hops: int, x: str = "load",
           caption: str | None = None, source: str = "best",
           tx_power_dbm: float = 2.0) -> None:
    v1 = _cells_by_key(_load(v1_dir))
    v2 = _cells_by_key(_load(v2_dir))
    keys = sorted(set(v1) & set(v2))
    if not keys:
        raise SystemExit("no matching (SF, load, size) cells present in BOTH campaigns")
    out_dir.mkdir(parents=True, exist_ok=True)

    # Cells that must not read as a v1-vs-v2 outcome (0-on-both / stall). Logged, not
    # silently dropped — the shaded columns are themselves an honest result.
    invalid = _invalid_cells(keys, v1, v2)
    for k in keys:
        if k in invalid:
            print(f"  NOTE: cell {_xlabel(k, x).replace(chr(10),' ')} marked invalid "
                  f"({invalid[k].replace(chr(10),' ')})")

    _load_note = "equal offered load" if x == "load" else "fixed send interval"
    _bar_figure(keys, v1, v2, "mean_current_mA", "Mean radio current (mA)",
                "Energy: mean radio current — Previous vs T-LoRaMesher",
                out_dir / "energy_mean_current.png", x=x)
    _bar_figure(keys, v1, v2, "energy_per_bit_mJ", "Energy per delivered bit (mJ)",
                "Energy per delivered application bit — Previous vs T-LoRaMesher",
                out_dir / "energy_per_delivered_bit.png", x=x, invalid=invalid)
    # Fair PDR = delivered / intended, intended = packet_count × DESIGNED sources.
    # Scores un-joined sources as undelivered load (see app_pdr.py); the participation
    # panel below discloses how many sources actually joined per cell.
    _bar_figure(keys, v1, v2, "app_pdr_fair", "Application PDR (delivered / intended)",
                None, out_dir / "app_pdr.png", x=x, invalid=invalid, caption=caption)
    # Honesty companion: fraction of designed sources that actually sent. Exposes
    # WHY v1's full-topology PDR differs (its remote cluster failed to join).
    _bar_figure(keys, v1, v2, "sender_participation",
                "Source participation (fraction of designed)",
                f"Sources that joined and sent — Previous vs T-LoRaMesher ({_load_note})",
                out_dir / "sender_participation.png", x=x, invalid=invalid,
                caption=caption)
    _bar_figure(keys, v1, v2, "app_delivered", "Delivered packets",
                f"Application packets delivered — Previous vs T-LoRaMesher ({_load_note})",
                out_dir / "app_delivered_count.png", x=x, invalid=invalid)
    # The latency pair is printed side by side at half width; the rest stand alone.
    _bar_figure(keys, v1, v2, "app_latency_p50_ms", "End-to-end latency p50 (s)",
                None, scale=1e-3, logy=True,
                out=out_dir / "app_latency_p50.png", x=x, invalid=invalid,
                figsize=FIG_HALF)
    _bar_figure(keys, v1, v2, "app_latency_p95_ms", "End-to-end latency p95 (s)",
                None, scale=1e-3, logy=True,
                out=out_dir / "app_latency_p95.png", x=x, invalid=invalid,
                figsize=FIG_HALF)
    if x == "size":
        # Packet-size sweep headline: does goodput hold as payload grows? Invalid
        # (oversized-payload) cells are shaded — both stacks deliver 0 there.
        _bar_figure(keys, v1, v2, "app_goodput_Bps", "Application goodput (B/s)",
                    "Application goodput vs payload size — Previous vs T-LoRaMesher",
                    out_dir / "app_goodput.png", x=x, invalid=invalid, caption=caption)
    _crossover_figure(v1, keys, out_dir / "energy_sleep_crossover.png",
                      nodes=nodes, max_hops=max_hops, source=source,
                      tx_power_dbm=tx_power_dbm)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Compare two campaigns (v1 vs v2) of the same batch")
    ap.add_argument("--v1", type=Path, required=True,
                    help="v1 campaign run dir (e.g. runs/sim_load_compare_prev)")
    ap.add_argument("--v2", type=Path, required=True,
                    help="v2 campaign run dir (e.g. runs/sim_load_compare)")
    ap.add_argument("--out", type=Path, default=Path("docs/paper/figures/energy"),
                    help="output directory for the PNGs")
    ap.add_argument("--nodes", type=int, default=13,
                    help="node count for the v2 crossover model projection")
    ap.add_argument("--max-hops", type=int, default=3,
                    help="max hop depth for the v2 crossover model projection")
    ap.add_argument("--x", choices=("load", "size"), default="load",
                    help="x-axis variable: 'load' (send interval, default) or "
                         "'size' (payload bytes, for the packet-size sweep)")
    ap.add_argument("--source", choices=SOURCES, default="best",
                    help="per-state current table for the crossover model (see "
                         "analysis/radio_power.py); must match the --source the "
                         "aggregated.json was built with")
    ap.add_argument("--tx-power", type=float, default=2.0, metavar="DBM",
                    help="TX power for the crossover model (default 2 dBm, the "
                         "testbed's dense-cluster setting)")
    ap.add_argument("--caption", type=str, default=None,
                    help="limitations footnote drawn under the PDR/participation/goodput "
                         "figures (e.g. the non-interleaved / marginal-link caveat)")
    args = ap.parse_args()
    render(args.v1, args.v2, args.out, nodes=args.nodes, max_hops=args.max_hops,
           x=args.x, caption=args.caption, source=args.source,
           tx_power_dbm=args.tx_power)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
