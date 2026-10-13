#!/usr/bin/env python3
"""Energy figures from a Nordic PPK2 current trace.

    python3 ppk2_report.py release/bench/energy/2026-10-13_COM3_E3_5v0.csv [--start 120]
    python3 ppk2_report.py release/bench/energy/2026-10-13_COM3_E3_5v0/   (ppk2_bench.py capture)

Inputs: a CSV exported by the nRF Connect Power Profiler app (columns found by header, e.g.
"Timestamp(ms),Current(uA)[,D0-D7]"; units taken from the header), the app's own session file
(<log folder>/<session id>/session.raw: float32 uA + uint16 digital bits per sample at
--raw-rate, 100 kS/s by default) or a ppk2_bench.py capture folder (trace.csv with 1 ms
mean/max bins). Large exports are read in chunks and reduced to
--bin-ms bins (mean and max), so the charge stays exact.

Per trace it writes <name>.json and <name>.png next to the input (or in --out):
average, median, 95th percentile and peak current, charge (mAh), energy (mWh), average power,
the bursts above baseline + --threshold-ma (LoRa TX, WiFi) with their extra charge, the
superframe period and its folded current profile (--period auto|none|<s>), and the battery
life of a cell at that average power. --battery-ratio converts a 5 V input-path power into the
battery-path power (E3-BAT / E3 from the same board).

The supply voltage comes from --voltage, the capture, or the file name (_5v0, _3v8).
"""
import argparse
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

import numpy as np

CAPTURE_TAG = "# ppk2_bench capture:"
TIME_UNITS = {"s": 1.0, "ms": 1e-3, "us": 1e-6}
CURRENT_UNITS = {"a": 1e6, "ma": 1e3, "ua": 1.0, "na": 1e-3}
TOP_BURSTS = 20
SESSION_RAW = "session.raw"
RAW_RECORD = np.dtype([("ua", "<f4"), ("bits", "<u2")])
APP_RATE = 100_000


@dataclass
class Trace:
    """Current in fixed bins starting at 0 s; empty bins are NaN."""

    bin_s: float
    mean_ua: np.ndarray
    max_ua: np.ndarray
    epoch: Optional[float] = None
    voltage: Optional[float] = None


def voltage_from_name(name: str) -> Optional[float]:
    match = re.search(r"(?<![0-9])([0-9])v([0-9])(?![0-9])", Path(name).name, re.IGNORECASE)
    return float(f"{match.group(1)}.{match.group(2)}") if match else None


def _unit(column: str, units: dict) -> float:
    match = re.search(r"\(([^)]+)\)", column)
    unit = match.group(1).strip().lower().replace("µ", "u") if match else ""
    if unit not in units:
        raise ValueError(f"unknown unit in column {column!r}")
    return units[unit]


class _Bins:
    """Accumulates sum, count and max per bin index."""

    def __init__(self):
        self.sum = np.zeros(0)
        self.count = np.zeros(0, dtype=np.int64)
        self.max = np.zeros(0)

    def _grow(self, size: int) -> None:
        if size <= self.sum.size:
            return
        extra = max(size, 2 * self.sum.size) - self.sum.size
        self.sum = np.concatenate([self.sum, np.zeros(extra)])
        self.count = np.concatenate([self.count, np.zeros(extra, dtype=np.int64)])
        self.max = np.concatenate([self.max, np.full(extra, -np.inf)])

    def add(self, index: np.ndarray, values: np.ndarray) -> None:
        """@p index must be sorted."""
        if index.size == 0:
            return
        starts = np.concatenate([[0], np.flatnonzero(np.diff(index)) + 1])
        bins = index[starts]
        self._grow(int(bins[-1]) + 1)
        self.sum[bins] += np.add.reduceat(values, starts)
        self.count[bins] += np.diff(np.concatenate([starts, [index.size]]))
        self.max[bins] = np.maximum(self.max[bins], np.maximum.reduceat(values, starts))

    def trace(self, bin_s: float, **extra) -> Trace:
        used = int(np.flatnonzero(self.count)[-1]) + 1 if self.count.any() else 0
        count = self.count[:used]
        with np.errstate(invalid="ignore", divide="ignore"):
            mean = np.where(count > 0, self.sum[:used] / count, np.nan)
        peak = np.where(count > 0, self.max[:used], np.nan)
        return Trace(bin_s=bin_s, mean_ua=mean, max_ua=peak, **extra)


def _read_capture(path: Path, first_line: str) -> Trace:
    import pandas as pd

    fields = dict(re.findall(r"(\w+)=([-0-9.eE+]+)", first_line))
    bin_s = float(fields["bin_s"])
    data = pd.read_csv(path, skiprows=1)
    index = np.rint(data["t_s"].to_numpy() / bin_s).astype(np.int64)
    size = int(index.max()) + 1 if index.size else 0
    mean = np.full(size, np.nan)
    peak = np.full(size, np.nan)
    mean[index] = data["mean_uA"].to_numpy()
    peak[index] = data["max_uA"].to_numpy()
    return Trace(bin_s=bin_s, mean_ua=mean, max_ua=peak,
                 epoch=float(fields["epoch"]) if "epoch" in fields else None,
                 voltage=float(fields["voltage"]) if "voltage" in fields else None)


def _read_session_raw(path: Path, bin_s: float, rate: int, chunksize: int) -> Trace:
    bins = _Bins()
    records = np.memmap(path, dtype=RAW_RECORD, mode="r")
    for first in range(0, records.size, chunksize):
        current = records["ua"][first:first + chunksize].astype(float)
        index = np.floor((first + np.arange(current.size)) / (rate * bin_s) + 1e-6)
        bins.add(index.astype(np.int64), current)
    return bins.trace(bin_s, voltage=voltage_from_name(path.parent.name))


def read_trace(path, bin_s: float = 0.001, chunksize: int = 1_000_000,
               raw_rate: int = APP_RATE) -> Trace:
    """Reads an nRF Power Profiler CSV export or session.raw, or a ppk2_bench capture (file or
    folder)."""
    import pandas as pd

    path = Path(path)
    if path.is_dir():
        path = path / SESSION_RAW if (path / SESSION_RAW).exists() else path / "trace.csv"
    if path.name == SESSION_RAW:
        return _read_session_raw(path, bin_s, raw_rate, chunksize)
    with open(path) as source:
        first_line = source.readline().strip()
    if first_line.startswith(CAPTURE_TAG):
        return _read_capture(path, first_line)

    columns = [name.strip() for name in first_line.split(",")]
    time_column = next(name for name in columns if "timestamp" in name.lower())
    current_column = next(name for name in columns if "current" in name.lower())
    time_scale = _unit(time_column, TIME_UNITS)
    current_scale = _unit(current_column, CURRENT_UNITS)

    bins = _Bins()
    origin = None
    for chunk in pd.read_csv(path, usecols=[time_column, current_column], chunksize=chunksize,
                             skipinitialspace=True):
        seconds = chunk[time_column].to_numpy(dtype=float) * time_scale
        current = chunk[current_column].to_numpy(dtype=float) * current_scale
        if origin is None and seconds.size:
            origin = seconds[0]
        index = np.floor((seconds - origin) / bin_s + 1e-6).astype(np.int64)
        bins.add(index, current)
    return bins.trace(bin_s, voltage=voltage_from_name(path.name))


def write_capture(path, trace: Trace, voltage: Optional[float] = None) -> None:
    """Writes @p trace in the ppk2_bench capture format (empty bins are left out)."""
    voltage = voltage if voltage is not None else trace.voltage
    header = f"{CAPTURE_TAG} bin_s={trace.bin_s:g}"
    if trace.epoch is not None:
        header += f" epoch={trace.epoch:.3f}"
    if voltage is not None:
        header += f" voltage={voltage:.3f}"
    with open(path, "w") as out:
        out.write(header + "\nt_s,mean_uA,max_uA\n")
        for index in np.flatnonzero(~np.isnan(trace.mean_ua)):
            out.write(f"{index * trace.bin_s:.6f},{trace.mean_ua[index]:.1f},"
                      f"{trace.max_ua[index]:.1f}\n")


def _window(trace: Trace, start: Optional[float], end: Optional[float]) -> Tuple[int, int]:
    first = 0 if start is None else max(0, math.ceil(start / trace.bin_s - 1e-9))
    last = trace.mean_ua.size if end is None else min(trace.mean_ua.size,
                                                      math.ceil(end / trace.bin_s - 1e-9))
    return first, last


def _bursts(mean_ma: np.ndarray, baseline_ma: float, threshold_ma: float, bin_s: float,
            merge_s: float, offset_s: float) -> dict:
    above = np.nan_to_num(mean_ma, nan=baseline_ma) > baseline_ma + threshold_ma
    edges = np.flatnonzero(np.diff(np.concatenate([[0], above.astype(np.int8), [0]])))
    runs: List[List[int]] = []
    gap = max(0, int(round(merge_s / bin_s)))
    for begin, end in zip(edges[::2], edges[1::2]):
        if runs and begin - runs[-1][1] <= gap:
            runs[-1][1] = end
        else:
            runs.append([begin, end])
    found = []
    for begin, end in runs:
        part = mean_ma[begin:end]
        found.append({"start_s": round(offset_s + begin * bin_s, 4),
                      "duration_s": round((end - begin) * bin_s, 4),
                      "mean_ma": round(float(np.nanmean(part)), 3),
                      "excess_mas": float(np.nansum(part - baseline_ma) * bin_s)})
    excess = sum(burst["excess_mas"] for burst in found)
    found.sort(key=lambda burst: burst["excess_mas"], reverse=True)
    for burst in found:
        burst["excess_mas"] = round(burst["excess_mas"], 4)
    return {"threshold_ma": threshold_ma, "count": len(found),
            "total_s": round(sum(burst["duration_s"] for burst in found), 4),
            "excess_mas": excess, "largest": found[:TOP_BURSTS]}


def battery_life(avg_mw: float, cell_mah: float = 3000.0, cell_v: float = 3.7,
                 usable: float = 0.8, ratio: Optional[float] = None) -> dict:
    """Hours a cell lasts at @p avg_mw (times @p ratio for the battery path), no solar input."""
    battery_mw = avg_mw * (ratio if ratio else 1.0)
    hours = cell_mah * cell_v * usable / battery_mw if battery_mw > 0 else math.inf
    return {"cell_mah": cell_mah, "cell_v": cell_v, "usable": usable, "ratio": ratio,
            "battery_mw": battery_mw, "hours": hours, "days": hours / 24.0}


def analyze(trace: Trace, voltage: Optional[float] = None, start: Optional[float] = None,
            end: Optional[float] = None, threshold_ma: float = 30.0,
            merge_s: float = 0.01) -> dict:
    """Charge, energy, current statistics and bursts of @p trace between @p start and @p end."""
    voltage = voltage if voltage is not None else trace.voltage
    first, last = _window(trace, start, end)
    mean_ma = trace.mean_ua[first:last] / 1000.0
    valid = mean_ma[~np.isnan(mean_ma)]
    if valid.size == 0:
        raise ValueError("no samples in the selected window")
    duration_s = valid.size * trace.bin_s
    avg_ma = float(valid.mean())
    baseline_ma = float(np.median(valid))
    charge_mah = avg_ma * duration_s / 3600.0
    result = {
        "start_s": first * trace.bin_s,
        "duration_s": duration_s,
        "missing_s": (mean_ma.size - valid.size) * trace.bin_s,
        "voltage": voltage,
        "avg_ma": avg_ma,
        "median_ma": baseline_ma,
        "p95_ma": float(np.percentile(valid, 95)),
        "peak_ma": float(np.nanmax(trace.max_ua[first:last])) / 1000.0,
        "baseline_ma": baseline_ma,
        "charge_mah": charge_mah,
        "bursts": _bursts(mean_ma, baseline_ma, threshold_ma, trace.bin_s, merge_s,
                          first * trace.bin_s),
    }
    if trace.epoch is not None:
        result["epoch_start"] = trace.epoch + first * trace.bin_s
    if voltage is not None:
        result["avg_mw"] = avg_ma * voltage
        result["energy_mwh"] = charge_mah * voltage
    return result


def fold(trace: Trace, period_s: Optional[float] = None, min_s: float = 20.0,
         max_s: float = 200.0, resolution_s: float = 0.05, start: Optional[float] = None,
         end: Optional[float] = None) -> Tuple[Optional[float], Optional[np.ndarray]]:
    """Superframe period (autocorrelation, unless given) and the mean current profile (mA) over
    one period. @return (None, None) when the trace is shorter than two periods."""
    first, last = _window(trace, start, end)
    mean = trace.mean_ua[first:last] / 1000.0
    factor = max(1, int(round(resolution_s / trace.bin_s)))
    usable = mean[:mean.size // factor * factor].reshape(-1, factor)
    with np.errstate(invalid="ignore"):
        x = np.nanmean(usable, axis=1) if usable.size else np.zeros(0)
    if x.size == 0 or np.isnan(x).all():
        return None, None
    x = np.where(np.isnan(x), np.nanmean(x), x)
    step = factor * trace.bin_s

    if period_s is None:
        if x.size * step < 2 * min_s:
            return None, None
        centered = x - x.mean()
        size = 1 << (2 * centered.size - 1).bit_length()
        spectrum = np.fft.rfft(centered, size)
        ac = np.fft.irfft(spectrum * np.conj(spectrum), size)[:centered.size]
        ac /= np.arange(centered.size, 0, -1)
        low = max(1, int(min_s / step))
        high = min(centered.size // 2, int(max_s / step) + 1)
        if high <= low + 2:
            return None, None
        lags = np.arange(low, high)
        values = ac[low:high]
        peaks = [i for i in range(1, values.size - 1)
                 if values[i] >= values[i - 1] and values[i] >= values[i + 1]]
        if not peaks:
            return None, None
        best = max(values[i] for i in peaks)
        pick = next(i for i in peaks if values[i] >= 0.9 * best)
        a, b, c = values[pick - 1], values[pick], values[pick + 1]
        shift = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
        period_s = (lags[pick] + shift) * step

    bins = max(1, int(math.ceil(period_s / step)))
    phase = np.floor(((np.arange(x.size) * step) % period_s) / step).astype(int)
    phase = np.minimum(phase, bins - 1)
    counts = np.bincount(phase, minlength=bins)
    with np.errstate(invalid="ignore"):
        profile = np.bincount(phase, weights=x, minlength=bins) / counts
    return float(period_s), profile


def plot(path: Path, trace: Trace, result: dict, profile: Optional[np.ndarray],
         points: int = 20000) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    first, last = _window(trace, result["start_s"], result["start_s"] + result["duration_s"] +
                          result["missing_s"])
    mean = trace.mean_ua[first:last] / 1000.0
    peak = trace.max_ua[first:last] / 1000.0
    factor = max(1, int(math.ceil(mean.size / points)))
    size = mean.size // factor * factor
    with np.errstate(invalid="ignore"):
        mean_d = np.nanmean(mean[:size].reshape(-1, factor), axis=1)
        peak_d = np.nanmax(peak[:size].reshape(-1, factor), axis=1)
    t = (first + np.arange(mean_d.size) * factor) * trace.bin_s

    rows = 2 if profile is not None else 1
    figure, axes = plt.subplots(rows, 1, figsize=(12, 4 * rows), squeeze=False)
    top = axes[0][0]
    top.fill_between(t, mean_d, peak_d, color="tab:orange", alpha=0.3, linewidth=0, label="max")
    top.plot(t, mean_d, color="tab:blue", linewidth=0.6, label="mean")
    top.axhline(result["avg_ma"], color="tab:red", linewidth=0.8,
                label=f"average {result['avg_ma']:.2f} mA")
    top.set_xlabel("time (s)")
    top.set_ylabel("current (mA)")
    top.set_title(path.stem)
    top.legend(loc="upper right")
    if profile is not None:
        bottom = axes[1][0]
        step = result["period_s"] / profile.size
        bottom.plot(np.arange(profile.size) * step, profile, color="tab:blue", linewidth=0.8)
        bottom.set_xlabel(f"time in superframe (s), period {result['period_s']:.2f} s")
        bottom.set_ylabel("mean current (mA)")
    figure.tight_layout()
    figure.savefig(path, dpi=110)
    plt.close(figure)


def _parse_period(text: str) -> Optional[str]:
    if text in ("auto", "none"):
        return text
    float(text)
    return text


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("traces", nargs="+", type=Path,
                        help="nRF Power Profiler CSV export or ppk2_bench capture folder")
    parser.add_argument("--voltage", type=float, help="supply voltage (V)")
    parser.add_argument("--start", type=float, help="window start (s from the trace start)")
    parser.add_argument("--end", type=float, help="window end (s)")
    parser.add_argument("--bin-ms", type=float, default=1.0, help="bin width for app recordings")
    parser.add_argument("--raw-rate", type=int, default=APP_RATE,
                        help="samples per second of an app session.raw")
    parser.add_argument("--threshold-ma", type=float, default=30.0,
                        help="a burst is above baseline + this (mA)")
    parser.add_argument("--period", type=_parse_period, default="auto",
                        help="superframe period: auto, none or seconds")
    parser.add_argument("--min-period", type=float, default=20.0)
    parser.add_argument("--max-period", type=float, default=200.0)
    parser.add_argument("--battery-ratio", type=float,
                        help="battery-path power / input-path power (E3-BAT / E3)")
    parser.add_argument("--cell-mah", type=float, default=3000.0)
    parser.add_argument("--cell-v", type=float, default=3.7)
    parser.add_argument("--usable", type=float, default=0.8)
    parser.add_argument("--out", type=Path, help="output folder (default: next to the input)")
    args = parser.parse_args(argv)

    for source in args.traces:
        trace = read_trace(source, bin_s=args.bin_ms / 1000.0, raw_rate=args.raw_rate)
        result = analyze(trace, voltage=args.voltage, start=args.start, end=args.end,
                         threshold_ma=args.threshold_ma)
        result["source"] = str(source)
        profile = None
        if args.period != "none":
            given = None if args.period == "auto" else float(args.period)
            period, profile = fold(trace, given, args.min_period, args.max_period,
                                   start=args.start, end=args.end)
            if period is not None:
                result["period_s"] = period
                result["profile_step_s"] = period / profile.size
                result["profile_ma"] = [round(float(value), 3) for value in profile]
        if result.get("avg_mw") is not None:
            result["battery"] = battery_life(result["avg_mw"], args.cell_mah, args.cell_v,
                                             args.usable, args.battery_ratio)

        name = source.name if source.is_dir() else source.stem
        if source.name == SESSION_RAW:
            name = source.parent.name
        folder = args.out or (source if source.is_dir() else source.parent)
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"{name}.json").write_text(json.dumps(result, indent=2) + "\n")
        plot(folder / f"{name}.png", trace, result, profile)

        line = (f"{name}: {result['duration_s']:.1f} s, avg {result['avg_ma']:.2f} mA, "
                f"median {result['median_ma']:.2f} mA, peak {result['peak_ma']:.1f} mA, "
                f"{result['charge_mah']:.4f} mAh, {result['bursts']['count']} bursts")
        if result.get("avg_mw") is not None:
            line += (f", {result['avg_mw']:.1f} mW at {result['voltage']:.2f} V, "
                     f"{result['energy_mwh']:.3f} mWh")
        if "period_s" in result:
            line += f", superframe {result['period_s']:.2f} s"
        if "battery" in result:
            line += f", battery {result['battery']['days']:.1f} days"
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
