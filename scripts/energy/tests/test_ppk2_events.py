import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ppk2_events as pe  # noqa: E402
import ppk2_report as pr  # noqa: E402


def trace_with_bursts(bursts, seconds=20.0, base_ua=34_000.0, epoch=1000.0):
    mean = np.full(int(seconds * 1000), base_ua)
    for start, length, ma in bursts:
        mean[int(start * 1000):int((start + length) * 1000)] = ma * 1000
    return pr.Trace(bin_s=0.001, mean_ua=mean, max_ua=mean.copy(), epoch=epoch)


def test_bursts_report_duration_and_charge_above_listening():
    trace = trace_with_bursts([(2.0, 0.2, 150.0), (9.0, 0.7, 150.0)])
    bursts = pe.tx_bursts(trace, threshold_ma=100.0, listen_ma=34.0)
    assert [round(b.start_s, 3) for b in bursts] == [2.0, 9.0]
    assert [round(b.duration_s, 3) for b in bursts] == [0.2, 0.7]
    assert bursts[1].excess_mas == pytest.approx(0.7 * 116.0)


def test_received_packets_match_the_burst_that_ended_just_before():
    trace = trace_with_bursts([(2.0, 0.2, 150.0), (9.0, 0.7, 150.0), (15.0, 0.3, 150.0)])
    bursts = pe.tx_bursts(trace, threshold_ma=100.0, listen_ma=34.0)
    log = ["1009.95\t[INFO] PKT_RX src=0x5E9C dst=0x6D4C type=0x11 size=120 rssi=-20.0",
           "1005.00\t[INFO] PKT_RX src=0x6778 dst=0x6D4C type=0x11 size=40 rssi=-20.0",
           "1012.00\t[INFO] PKT_RX src=0x5E9C dst=0x6D4C type=0x11 size=30 rssi=-20.0"]
    packets = pe.received_packets(log, source="5E9C")
    matched = pe.match(packets, bursts, trace.epoch, before_s=2.0)
    assert [(m.size, round(m.burst.duration_s, 3)) for m in matched] == [(120, 0.7)]
