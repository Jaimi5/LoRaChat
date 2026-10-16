#!/usr/bin/env python3
"""Charge of each packet a node sent, from a ppk2_bench capture and the gateway log.

    python3 ppk2_events.py release/bench/energy/<capture> --source 5E9C [--start 420]

The node's transmissions are the bursts above --threshold-ma in the trace; each packet the
gateway logged as received from --source (PKT_RX ... size=N) is matched to the burst that ended
within --before-s seconds before the log line (both on the PC clock). Prints, per packet size,
the count, the airtime and the charge above listening.
"""
import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

import numpy as np

import ppk2_report as pr

PKT_RX = re.compile(r"PKT_RX src=0x([0-9A-Fa-f]{4}) dst=0x[0-9A-Fa-f]{4} type=0x[0-9A-Fa-f]+ "
                    r"size=(\d+)")


@dataclass
class Burst:
    start_s: float
    duration_s: float
    excess_mas: float


@dataclass
class Packet:
    time: float
    size: int


@dataclass
class Match:
    size: int
    burst: Burst


def tx_bursts(trace: pr.Trace, threshold_ma: float = 100.0, listen_ma: float = 34.0,
              start: Optional[float] = None) -> List[Burst]:
    """@return the runs of bins above @p threshold_ma, with their charge above @p listen_ma."""
    first = 0 if start is None else int(start / trace.bin_s)
    mean_ma = np.nan_to_num(trace.mean_ua[first:] / 1000.0)
    above = np.concatenate([[0], (mean_ma > threshold_ma).astype(np.int8), [0]])
    edges = np.flatnonzero(np.diff(above))
    bursts = []
    for begin, end in zip(edges[::2], edges[1::2]):
        bursts.append(Burst(start_s=(first + begin) * trace.bin_s,
                            duration_s=(end - begin) * trace.bin_s,
                            excess_mas=float((mean_ma[begin:end] - listen_ma).sum() *
                                             trace.bin_s)))
    return bursts


def received_packets(lines: List[str], source: str) -> List[Packet]:
    packets = []
    for line in lines:
        stamp, _, text = line.partition("\t")
        match = PKT_RX.search(text)
        if match and match.group(1).upper() == source.upper():
            packets.append(Packet(time=float(stamp), size=int(match.group(2))))
    return packets


def match(packets: List[Packet], bursts: List[Burst], epoch: float,
          before_s: float = 2.0) -> List[Match]:
    """@return the packets whose burst ended within @p before_s before the gateway logged them."""
    found = []
    for packet in packets:
        at = packet.time - epoch
        candidates = [burst for burst in bursts
                      if 0.0 <= at - (burst.start_s + burst.duration_s) <= before_s]
        if candidates:
            found.append(Match(size=packet.size, burst=candidates[-1]))
    return found


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("capture", type=Path)
    parser.add_argument("--source", required=True, help="node address, e.g. 5E9C")
    parser.add_argument("--start", type=float)
    parser.add_argument("--threshold-ma", type=float, default=100.0)
    parser.add_argument("--listen-ma", type=float, default=34.0)
    parser.add_argument("--before-s", type=float, default=2.0)
    args = parser.parse_args(argv)

    trace = pr.read_trace(args.capture)
    bursts = tx_bursts(trace, args.threshold_ma, args.listen_ma, args.start)
    lines = (args.capture / "gateway.log").read_text(errors="replace").splitlines()
    matched = match(received_packets(lines, args.source), bursts, trace.epoch, args.before_s)
    by_size: Dict[int, List[Burst]] = {}
    for item in matched:
        by_size.setdefault(item.size, []).append(item.burst)
    print(f"{len(bursts)} bursts, {len(matched)} matched to received packets")
    for size in sorted(by_size):
        group = by_size[size]
        print(f"size {size:3d} B: {len(group):2d} packets, airtime "
              f"{np.mean([b.duration_s for b in group]) * 1000:.0f} ms, charge above listening "
              f"{np.mean([b.excess_mas for b in group]):.1f} mA*s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
