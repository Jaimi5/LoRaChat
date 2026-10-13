#!/usr/bin/env python3
"""Comparison figures for a set of ppk2_report.py results.

    python3 ppk2_compare.py release/bench/energy/campaign.json [--out release/bench/energy/compare]

The campaign file lists the results in the order they are compared, each in a group:

    [{"group": "sleep", "label": "none", "report": "2026-10-13_COM3_E3-N4_5v0/....json"}, ...]

("report" is relative to the campaign file.) Per group it draws <group>.png: the average
power of each row with the battery life on the bars, and, for rows with a superframe profile,
the mean current over one superframe (aligned on the node's own transmission). summary.md has one table of all rows with the saving
of each row against the first row of its group.
"""
import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List


def load_campaign(path: Path) -> List[dict]:
    """@return one row per campaign entry whose report exists, in file order."""
    rows = []
    for entry in json.loads(Path(path).read_text()):
        source = Path(path).parent / entry["report"]
        if not source.exists():
            print(f"missing report {entry['report']}, skipped", file=sys.stderr)
            continue
        row = dict(entry)
        row.update(json.loads(source.read_text()))
        rows.append(row)
    return rows


def saving(rows: List[dict]) -> List[float]:
    """@return each row's power saving against the first row, in percent."""
    base = rows[0]["avg_mw"]
    return [100.0 * (1.0 - row["avg_mw"] / base) for row in rows]


def align(profile: List[float], at: float = 0.9) -> List[float]:
    """@return @p profile rotated so its highest point (the node's transmission) sits at the
    fraction @p at of the superframe, so profiles recorded from different starts line up."""
    shift = int(round(at * len(profile))) - profile.index(max(profile))
    return profile[-shift % len(profile):] + profile[:-shift % len(profile)]


def groups(rows: List[dict]) -> Dict[str, List[dict]]:
    found: Dict[str, List[dict]] = {}
    for row in rows:
        found.setdefault(row["group"], []).append(row)
    return found


def plot_group(path: Path, name: str, rows: List[dict]) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    profiles = [row for row in rows if "profile_ma" in row]
    columns = 2 if profiles else 1
    figure, axes = plt.subplots(1, columns, figsize=(7 * columns, 4.5), squeeze=False)
    bars = axes[0][0]
    labels = [row["label"] for row in rows]
    values = [row["avg_mw"] for row in rows]
    drawn = bars.bar(range(len(rows)), values, color="tab:blue")
    for bar, row in zip(drawn, rows):
        days = row.get("battery", {}).get("days")
        text = f"{row['avg_mw']:.0f} mW"
        if days is not None:
            text += f"\n{days:.1f} d"
        bars.annotate(text, (bar.get_x() + bar.get_width() / 2, bar.get_height()),
                      ha="center", va="bottom", fontsize=8)
    bars.set_xticks(range(len(rows)))
    bars.set_xticklabels(labels, rotation=20, ha="right")
    bars.set_ylabel("average power (mW)")
    bars.set_ylim(0, max(values) * 1.25)
    bars.set_title(f"{name}: average power, battery life (3000 mAh)")
    if profiles:
        curves = axes[0][1]
        for row in profiles:
            step = row["profile_step_s"]
            times = [index * step for index in range(len(row["profile_ma"]))]
            curves.plot(times, align(row["profile_ma"]), linewidth=0.8,
                        label=f"{row['label']} ({row['period_s']:.1f} s)")
        curves.set_xlabel("time in superframe (s), aligned on the node's transmission")
        curves.set_ylabel("mean current (mA)")
        curves.set_title(f"{name}: one superframe")
        curves.legend(fontsize=8)
    figure.tight_layout()
    figure.savefig(path, dpi=110)
    plt.close(figure)


def summary(rows: List[dict]) -> str:
    lines = ["| group | test | avg mA | avg mW | superframe s | battery days | saving |",
             "|---|---|---|---|---|---|---|"]
    for name, members in groups(rows).items():
        for row, saved in zip(members, saving(members)):
            period = f"{row['period_s']:.1f}" if "period_s" in row else ""
            days = row.get("battery", {}).get("days")
            lines.append(f"| {name} | {row['label']} | {row['avg_ma']:.2f} | "
                         f"{row['avg_mw']:.1f} | {period} | "
                         f"{'' if days is None else f'{days:.1f}'} | {saved:.0f} % |")
    return "\n".join(lines) + "\n"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--out", type=Path, help="output folder (default: <campaign>/../compare)")
    args = parser.parse_args(argv)

    rows = load_campaign(args.campaign)
    if not rows:
        print("no results", file=sys.stderr)
        return 1
    out = args.out or args.campaign.parent / "compare"
    out.mkdir(parents=True, exist_ok=True)
    for name, members in groups(rows).items():
        plot_group(out / f"{name}.png", name, members)
    (out / "summary.md").write_text(summary(rows))
    print(summary(rows), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
