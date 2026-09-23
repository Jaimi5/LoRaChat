"""Plot the long-link (006C<->3428) RSSI/SNR/reception corpus, with weather overlay.

Consumes the CSVs written by link_health_corpus.py (and, if present, weather_hourly.csv
from weather_backfill.py) and renders the figure set into the same directory:

  longlink_multiday       RSSI & SNR vs calendar date (day-to-day channel variation)
  longlink_diurnal        RSSI, SNR, reception rate vs local hour-of-day (per SF)
  longlink_reception_rate packets/hour over calendar time (the survivorship-safe metric)
  longlink_weather        RSSI/SNR/reception vs humidity/temperature (+ correlation)
  longlink_july_v1_v2     v1 (Jul 8-9) vs v2 (Jul 10) long-link SNR & reception + weather

Reception rate — NOT decode-conditioned RSSI — is the primary health signal: fades reduce
the number of surviving samples, not their RSSI (survivorship bias). v1 rows are an
RSSI-threshold heuristic (confidence=v1_heuristic); they are drawn distinctly.

Run from scripts/testbed/:
    python3 analysis/plot_link_health.py --dir docs/paper/figures/long_link
"""

from __future__ import annotations

import argparse
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

# Semtech SX1276 @ BW125 receiver sensitivity and demod SNR limit per SF (datasheet).
SF_SENSITIVITY_DBM = {7: -123, 8: -126, 9: -129, 10: -132, 11: -134.5, 12: -137}
SF_SNR_LIMIT_DB = {7: -7.5, 8: -10.0, 9: -12.5, 10: -15.0, 11: -17.5, 12: -20.0}

VERSION_COLOR = {"v1": "#c1440e", "v2": "#1f6fb2"}


def _load(dir_: Path):
    import pandas as pd
    corpus = pd.read_csv(dir_ / "link_health_corpus.csv")
    corpus["dt"] = pd.to_datetime(corpus["ts_utc"], unit="s", utc=True)
    rate = _maybe_csv(dir_ / "link_health_reception_rate.csv")
    if rate is not None and len(rate):
        rate["dt"] = pd.to_datetime(rate["hour_utc"], unit="s", utc=True)
    weather = _maybe_csv(dir_ / "weather_hourly.csv")
    if weather is not None and len(weather):
        # weather 'time' is local (Europe/Madrid) naive ISO; localize then convert to UTC
        weather["dt"] = (pd.to_datetime(weather["time"])
                         .dt.tz_localize("Europe/Madrid", nonexistent="shift_forward",
                                         ambiguous="NaT")
                         .dt.tz_convert("UTC"))
    return corpus, rate, weather


def _maybe_csv(path: Path):
    import pandas as pd
    if not path.is_file():
        return None
    try:
        return pd.read_csv(path)
    except Exception:
        return None


def plot_multiday(corpus, dir_, save_fig):
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    for metric, ax, label in ((("rssi"), axes[0], "RSSI (dBm)"),
                              (("snr"), axes[1], "SNR (dB)")):
        for version, sub in corpus.groupby("version"):
            ax.scatter(sub["dt"], sub[metric], s=6, alpha=0.35,
                       color=VERSION_COLOR.get(version, "#666"),
                       label=f"{version} (n={len(sub)})")
        ax.set_ylabel(label)
        ax.grid(True, linestyle=":", alpha=0.5)
    # SF12 reference floors (the long link runs at SF12 in the observational batches)
    axes[0].axhline(SF_SENSITIVITY_DBM[12], color="k", ls="--", lw=0.8,
                    label="SF12 sensitivity (-137 dBm)")
    axes[1].axhline(SF_SNR_LIMIT_DB[12], color="k", ls="--", lw=0.8,
                    label="SF12 SNR limit (-20 dB)")
    axes[0].legend(fontsize=7, loc="upper right", ncol=2)
    axes[1].legend(fontsize=7, loc="upper right", ncol=2)
    axes[1].set_xlabel("date (UTC)")
    fig.suptitle("Long link 006C↔ 3428: RSSI/SNR across the campaign")
    fig.autofmt_xdate()
    save_fig(fig, dir_, "longlink_multiday")


def plot_diurnal(corpus, rate, dir_, save_fig):
    import matplotlib.pyplot as plt
    import numpy as np
    fig, axes = plt.subplots(3, 1, figsize=(8, 8), sharex=True)
    hours = np.arange(24)

    for metric, ax, label in (("rssi", axes[0], "RSSI (dBm)"),
                              ("snr", axes[1], "SNR (dB)")):
        for sf, sub in corpus.groupby("sf"):
            g = sub.groupby("hour_of_day")[metric]
            med = g.median().reindex(hours)
            p10 = g.quantile(0.10).reindex(hours)
            p90 = g.quantile(0.90).reindex(hours)
            line, = ax.plot(hours, med, marker="o", ms=3, label=f"SF{int(sf)}")
            ax.fill_between(hours, p10, p90, alpha=0.15, color=line.get_color())
        ax.set_ylabel(label)
        ax.grid(True, linestyle=":", alpha=0.5)
        ax.legend(fontsize=7, ncol=3)

    # Reception rate per hour-of-day (survivorship-safe): mean packets/hour-bin
    if rate is not None and len(rate):
        for version, sub in rate.groupby("version"):
            g = sub.groupby("hour_of_day")["packets"].mean().reindex(hours)
            axes[2].plot(hours, g, marker="s", ms=3,
                         color=VERSION_COLOR.get(version, "#666"), label=f"{version}")
        axes[2].legend(fontsize=7)
    axes[2].set_ylabel("mean packets / clock-hour")
    axes[2].set_xlabel("local hour of day (Europe/Madrid)")
    axes[2].set_xticks(range(0, 24, 2))
    axes[2].grid(True, linestyle=":", alpha=0.5)
    fig.suptitle("Long link 006C↔ 3428: diurnal RSSI / SNR / reception")
    save_fig(fig, dir_, "longlink_diurnal")


def plot_reception_rate(rate, dir_, save_fig):
    import matplotlib.pyplot as plt
    import pandas as pd
    if rate is None or not len(rate):
        return
    # Break the line across multi-hour gaps: runs are hours/days apart, so a continuous
    # line would draw a misleading "slow ramp" across periods with no experiment. Split
    # each version into segments separated by gaps > GAP_H hours and plot each segment.
    GAP_H = 3
    fig, ax = plt.subplots(figsize=(9, 3.5))
    for version, sub in rate.groupby("version"):
        agg = sub.groupby("dt")["packets"].sum().sort_index()
        if agg.empty:
            continue
        gap = agg.index.to_series().diff() > pd.Timedelta(hours=GAP_H)
        seg = gap.cumsum()
        color = VERSION_COLOR.get(version, "#666")
        first = True
        for _sid, s in agg.groupby(seg):
            ax.plot(s.index, s.values, marker=".", ms=4, lw=0.8, color=color,
                    label=(version if first else None))
            first = False
    ax.set_ylabel("packets / hour")
    ax.set_xlabel("date (UTC)")
    ax.grid(True, linestyle=":", alpha=0.5)
    ax.legend(fontsize=8)
    ax.set_title("Long-link reception rate over time (lines break across gaps with no runs)")
    fig.autofmt_xdate()
    save_fig(fig, dir_, "longlink_reception_rate")


def plot_weather(corpus, rate, weather, dir_, save_fig):
    import matplotlib.pyplot as plt
    import numpy as np
    import pandas as pd
    if weather is None or not len(weather):
        print("  (no weather_hourly.csv — skipping weather correlation)")
        return

    # Join each RSSI/SNR sample to the weather hour it falls in.
    c = corpus.dropna(subset=["rssi", "snr"]).copy()
    c["hour"] = c["dt"].dt.floor("h")
    w = weather.dropna(subset=["dt"]).copy()
    w["hour"] = w["dt"].dt.floor("h")
    merged = c.merge(w[["hour", "relative_humidity_2m", "temperature_2m", "precipitation"]],
                     on="hour", how="inner")
    if not len(merged):
        print("  (no overlap between samples and weather — skipping)")
        return

    fig, axes = plt.subplots(1, 2, figsize=(10, 4))
    for ax, xvar, xlabel in ((axes[0], "relative_humidity_2m", "relative humidity (%)"),
                             (axes[1], "temperature_2m", "temperature (°C)")):
        ax.scatter(merged[xvar], merged["snr"], s=6, alpha=0.3, color="#1f6fb2")
        # correlation coefficient
        m = merged.dropna(subset=[xvar, "snr"])
        if len(m) > 2:
            r = np.corrcoef(m[xvar], m["snr"])[0, 1]
            ax.set_title(f"SNR vs {xlabel.split(' (')[0]}  (r={r:+.2f}, n={len(m)})",
                         fontsize=9)
        ax.set_xlabel(xlabel)
        ax.set_ylabel("SNR (dB)")
        ax.grid(True, linestyle=":", alpha=0.5)
    fig.suptitle("Long-link SNR vs weather (decode-conditioned — see survivorship caveat)")
    fig.tight_layout()
    save_fig(fig, dir_, "longlink_weather")


def plot_july_v1_v2(corpus, rate, weather, dir_, save_fig):
    import matplotlib.pyplot as plt
    import pandas as pd
    # July campaign window
    jul = corpus[(corpus["dt"] >= "2026-07-08") & (corpus["dt"] < "2026-07-11")].copy()
    if not len(jul):
        print("  (no July long-link samples — skipping v1/v2 panel)")
        return
    fig, axes = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
    counts = {}
    for version, sub in jul.groupby("version"):
        counts[version] = len(sub)
        axes[0].scatter(sub["dt"], sub["snr"], s=12, alpha=0.6,
                        color=VERSION_COLOR.get(version, "#666"),
                        label=f"{version} SNR (n={len(sub)})")
    axes[0].axhline(SF_SNR_LIMIT_DB[12], color="k", ls="--", lw=0.8)
    axes[0].set_ylabel("SNR (dB)")
    axes[0].legend(fontsize=8, loc="lower left")
    axes[0].grid(True, linestyle=":", alpha=0.5)
    axes[0].set_title("July campaign: long-link decode SNR, v1 (Jul 8–9) vs v2 (Jul 10)")
    # Make the reception asymmetry legible — it is the headline, not the SNR spread.
    axes[0].annotate(
        f"clean long-link decodes at 3428:\n"
        f"v1 = {counts.get('v1', 0)} over ~48 h   vs   v2 = {counts.get('v2', 0)} over ~14 h\n"
        f"weather ranges overlap between the two windows",
        xy=(0.5, 0.97), xycoords="axes fraction", ha="center", va="top", fontsize=8,
        bbox=dict(boxstyle="round", fc="#fff4e6", ec="#c1440e", alpha=0.9))

    if weather is not None and len(weather):
        w = weather[(weather["dt"] >= "2026-07-08") & (weather["dt"] < "2026-07-11")]
        if len(w):
            ax2 = axes[1]
            ax2.plot(w["dt"], w["relative_humidity_2m"], color="#2a9d8f",
                     label="humidity (%)")
            ax2.set_ylabel("humidity (%)", color="#2a9d8f")
            ax2b = ax2.twinx()
            ax2b.plot(w["dt"], w["temperature_2m"], color="#e76f51", label="temp (°C)")
            ax2b.set_ylabel("temp (°C)", color="#e76f51")
            ax2.grid(True, linestyle=":", alpha=0.5)
    axes[1].set_xlabel("date/time (UTC)")
    fig.autofmt_xdate()
    save_fig(fig, dir_, "longlink_july_v1_v2")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, default=Path("docs/paper/figures/long_link"),
                    help="directory holding the corpus CSVs; figures written here too")
    args = ap.parse_args()

    import matplotlib
    matplotlib.use("Agg")
    try:
        from analysis.plot_paper import _set_pub_style, save_fig
        _set_pub_style()
    except Exception:
        # Fallback: minimal saver if the shared style helper is unavailable.
        def save_fig(fig, out_dir: Path, stem: str):
            out_dir.mkdir(parents=True, exist_ok=True)
            for ext in ("pdf", "png"):
                fig.savefig(out_dir / f"{stem}.{ext}")
                print(f"  wrote {out_dir / f'{stem}.{ext}'}")
            import matplotlib.pyplot as plt
            plt.close(fig)

    corpus, rate, weather = _load(args.dir)
    print(f"loaded {len(corpus)} rx samples; "
          f"rate rows={0 if rate is None else len(rate)}; "
          f"weather rows={0 if weather is None else len(weather)}")

    plot_multiday(corpus, args.dir, save_fig)
    plot_diurnal(corpus, rate, args.dir, save_fig)
    plot_reception_rate(rate, args.dir, save_fig)
    plot_weather(corpus, rate, weather, args.dir, save_fig)
    plot_july_v1_v2(corpus, rate, weather, args.dir, save_fig)
    print(f"figures written to {args.dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
