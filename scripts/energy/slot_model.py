#!/usr/bin/env python3
"""Average current of a LoRaMesher member from the superframe layout and measured currents.

The superframe follows LoRaMesher's SlotScheduler (feat/member-sleep 86a4072):
active slots = sync (hops + 1) + control (one per node, at most 50) + discovery 2 (hops + 1)
+ data (nodes x data slots, at most 50); total = max(16, active + 2, ceil(active / (1 - fraction))),
at most 255. A member listens (or sends) in every active slot and sleeps in the others:
the MCU stays awake (none), light-sleeps, or deep-sleeps when the SLEEP run is long enough
for a reboot (min sleep + boot budget + clock margin), paying a wake-up charge each time.

    python3 slot_model.py validate release/bench/energy/model.json
    python3 slot_model.py extrapolate release/bench/energy/model.json --out <folder>

model.json holds the measured currents and the bench runs to validate against.
"""
import argparse
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List

import numpy

MIN_SLOTS = 16
CHURN_MARGIN_SLOTS = 2
MAX_SLOTS = 255
MAX_NODES = 50
MAX_DATA_SLOTS = 50
CELL_MAH = 3000.0
CELL_V = 3.7
USABLE = 0.8


@dataclass
class Network:
    nodes: int
    hops: int = 1
    present: int = 0  # nodes still on air (default: all); an absent node's slots sleep
    data_slots: int = 1
    sleep_fraction: float = 0.0
    slot_s: float = 0.95
    sf: int = 9


@dataclass
class Slots:
    active: int
    total: int


@dataclass
class Currents:
    """Measured on one board (mA, and mA*s per event)."""

    awake_ma: float
    listen_ma: float
    light_ma: float
    deep_ma: float
    tx_base_mas: float
    tx_per_node_mas: float
    tx_cap_mas: float
    deep_wake_mas: float
    deep_min_s: float = 30.0
    deep_margin_s: float = 2.3
    extra: Dict[str, float] = field(default_factory=dict)


def superframe_slots(network: Network) -> Slots:
    layers = network.hops + 1
    nodes = min(network.nodes, MAX_NODES)
    active = layers + nodes + 2 * layers + min(nodes * network.data_slots, MAX_DATA_SLOTS)
    total = max(MIN_SLOTS, active + CHURN_MARGIN_SLOTS)
    if 0.0 < network.sleep_fraction < 1.0:
        # LoRaMesher divides in single precision: 12 / (1 - 0.85f) rounds up to 81
        sleeping = numpy.float32(1.0) - numpy.float32(network.sleep_fraction)
        total = max(total, math.ceil(float(numpy.float32(active) / sleeping)))
    return Slots(active=active, total=min(total, MAX_SLOTS))


def deep_sleeps(network: Network, currents: Currents) -> bool:
    slots = superframe_slots(network)
    run_s = (slots.total - slots.active) * network.slot_s
    return run_s - currents.deep_margin_s >= currents.deep_min_s


def airtime_ms(sf: int, size: int, bandwidth_khz: float = 125.0, coding_rate: int = 7,
               preamble: int = 8) -> float:
    """@return the LoRa time on air of @p size bytes (explicit header, CRC on; Semtech AN1200.13),
    with low data rate optimisation from SF11 at 125 kHz."""
    symbol_ms = (2 ** sf) / bandwidth_khz
    low_rate = 1 if symbol_ms > 16.0 else 0
    bits = 8 * size - 4 * sf + 28 + 16
    payload = 8 + max(math.ceil(bits / (4 * (sf - 2 * low_rate))) * coding_rate, 0)
    return (preamble + 4.25 + payload) * symbol_ms


TX_REFERENCE = (9, 43)  # the routing packet of the 4-node bench network, where tx_* were fitted


def tx_mas(network: Network, currents: Currents) -> float:
    """@return the transmit charge above listening per superframe: the control packet carries
    the routing table, so it grows with the network (capped by the packet size), and it scales
    with the airtime of the spreading factor."""
    nodes = network.present or network.nodes
    charge = min(currents.tx_base_mas + currents.tx_per_node_mas * nodes, currents.tx_cap_mas)
    scale = airtime_ms(network.sf, TX_REFERENCE[1]) / airtime_ms(*TX_REFERENCE)
    return charge * scale


def average_ma(network: Network, mode: str, currents: Currents) -> float:
    """@return the member's average current for sleep @p mode (none, light or deep)."""
    slots = superframe_slots(network)
    absent = network.nodes - network.present if network.present else 0
    awake_slots = slots.active - absent
    period_s = slots.total * network.slot_s
    sleep_s = (slots.total - awake_slots) * network.slot_s
    charge = awake_slots * network.slot_s * currents.listen_ma + tx_mas(network, currents)
    if mode == "none":
        charge += sleep_s * currents.awake_ma
    elif mode == "deep" and deep_sleeps(network, currents):
        charge += sleep_s * currents.deep_ma + currents.deep_wake_mas
    elif mode in ("light", "deep"):
        charge += sleep_s * currents.light_ma
    else:
        raise ValueError(f"unknown sleep mode {mode}")
    return charge / period_s


def battery_days(avg_ma: float, voltage: float) -> float:
    return CELL_MAH * CELL_V * USABLE / (avg_ma * voltage) / 24.0


def load(path: Path):
    data = json.loads(Path(path).read_text())
    return Currents(**data["currents"]), data


def validate(path: Path) -> List[dict]:
    currents, data = load(path)
    rows = []
    for run in data["runs"]:
        network = Network(**run["network"])
        predicted = average_ma(network, run["mode"], currents)
        rows.append({"label": run["label"], "measured_ma": run["measured_ma"],
                     "predicted_ma": predicted,
                     "error_pct": 100.0 * (predicted - run["measured_ma"]) / run["measured_ma"]})
    return rows


def extrapolate(path: Path, out: Path) -> None:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    currents, data = load(path)
    voltage = data.get("voltage", 4.5)
    out.mkdir(parents=True, exist_ok=True)
    nodes = list(range(2, 51))

    figure, axes = plt.subplots(1, 2, figsize=(13, 4.5))
    for fraction, style in ((0.0, ":"), (0.5, "--"), (0.85, "-")):
        for mode, color in (("none", "tab:gray"), ("light", "tab:blue"), ("deep", "tab:red")):
            values = [average_ma(Network(nodes=n, sleep_fraction=fraction), mode, currents)
                      for n in nodes]
            axes[0].plot(nodes, values, style, color=color,
                         label=f"{mode}, fraction {fraction:.0%}")
    axes[0].set_xlabel("nodes in the network")
    axes[0].set_ylabel("member average current (mA)")
    axes[0].set_yscale("log")
    axes[0].set_title("Model: member current vs network size")
    axes[0].legend(fontsize=7, ncol=3)

    fractions = [f / 100 for f in range(0, 91, 2)]
    for n, style in ((4, "-"), (10, "--"), (30, ":")):
        for mode, color in (("light", "tab:blue"), ("deep", "tab:red")):
            values = [battery_days(average_ma(Network(nodes=n, sleep_fraction=f), mode,
                                              currents), voltage) for f in fractions]
            axes[1].plot([f * 100 for f in fractions], values, style, color=color,
                         label=f"{mode}, {n} nodes")
    axes[1].set_xlabel("gateway sleep fraction (%)")
    axes[1].set_ylabel("battery life, 3000 mAh (days)")
    axes[1].set_title("Model: battery life vs sleep fraction")
    axes[1].legend(fontsize=7, ncol=2)
    figure.tight_layout()
    figure.savefig(out / "model_extrapolation.png", dpi=110)
    plt.close(figure)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("command", choices=["validate", "extrapolate"])
    parser.add_argument("model", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    if args.command == "validate":
        for row in validate(args.model):
            print(f"{row['label']}: measured {row['measured_ma']:.2f} mA, model "
                  f"{row['predicted_ma']:.2f} mA ({row['error_pct']:+.1f} %)")
    else:
        extrapolate(args.model, args.out or args.model.parent / "compare")
    return 0


if __name__ == "__main__":
    sys.exit(main())
