import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ppk2_bench as pb  # noqa: E402
import ppk2_report as pr  # noqa: E402


def test_binner_keeps_mean_and_max_per_bin():
    binner = pb.Binner(samples_per_bin=100)
    binner.add([10.0] * 99 + [1000.0])
    binner.add([20.0] * 150)
    binner.flush()
    assert binner.mean == pytest.approx([19.9, 20.0, 20.0])
    assert binner.max == [1000.0, 20.0, 20.0]


def test_schedule_sends_once_and_periodic_lines():
    schedule = pb.Schedule(at=["5:@5E9C /displayOn"], every=["2:@5E9C /version"])
    sent = []
    for tenth in range(0, 71):
        elapsed = tenth / 10
        sent += [(elapsed, line) for line in schedule.due(elapsed)]
    assert sent == [(2.0, "@5E9C /version"), (4.0, "@5E9C /version"),
                    (5.0, "@5E9C /displayOn"), (6.0, "@5E9C /version")]


def test_schedule_rejects_a_line_without_time():
    with pytest.raises(ValueError):
        pb.Schedule(at=["@5E9C /displayOn"], every=[])


def test_capture_folder_name():
    assert pb.capture_name("2026-10-13", "COM3", "E3", 5.0) == "2026-10-13_COM3_E3_5v0"
    assert pb.capture_name("2026-10-13", "COM9", "E3-BAT", 3.8) == "2026-10-13_COM9_E3-BAT_3v8"


class FakePpk2:
    """100 kS/s source: each get_data() is 10 ms of samples at 40 mA, 140 mA in the 2nd half
    second. Sample values travel as text so get_samples has something to decode."""

    def __init__(self, clock):
        self.clock = clock
        self.calls = []
        self.sent = 0

    def get_modifiers(self):
        self.calls.append("modifiers")

    def use_source_meter(self):
        self.calls.append("source")

    def set_source_voltage(self, mv):
        self.calls.append(f"voltage {mv}")

    def toggle_DUT_power(self, state):
        self.calls.append(f"power {state}")

    def start_measuring(self):
        self.calls.append("start")

    def stop_measuring(self):
        self.calls.append("stop")

    def get_data(self):
        self.clock.now += 0.01
        value = 140_000.0 if 0.5 <= self.sent / 100_000 < 1.0 else 40_000.0
        self.sent += 1000
        return f"{value}".encode()

    def get_samples(self, data):
        return [float(data.decode())] * 1000, [0] * 1000


class FakeClock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


class FakePort:
    def __init__(self):
        self.written = []

    def send(self, line):
        self.written.append(line)


def test_capture_drives_the_ppk2_and_writes_a_readable_trace(tmp_path):
    clock = FakeClock()
    ppk2 = FakePpk2(clock)
    gateway = FakePort()
    schedule = pb.Schedule(at=["0.5:@5E9C /displayOn"], every=[])
    meta = pb.capture(ppk2, tmp_path, seconds=1.0, voltage=5.0, power_cycle_s=2.0,
                      schedule=schedule, gateway=gateway, clock=clock, sleep=lambda s: None)
    assert ppk2.calls == ["modifiers", "source", "voltage 5000", "power OFF", "start",
                          "power ON", "stop"]
    assert gateway.written == ["@5E9C /displayOn"]
    assert meta["samples"] == 100_000
    trace = pr.read_trace(tmp_path)
    assert trace.voltage == pytest.approx(5.0)
    assert trace.epoch == pytest.approx(meta["epoch"])
    result = pr.analyze(trace)
    assert result["duration_s"] == pytest.approx(1.0)
    assert result["avg_ma"] == pytest.approx(90.0)
    assert np.nanmax(trace.max_ua) == pytest.approx(140_000.0)


def test_warmup_powers_the_board_before_the_recording_starts(tmp_path):
    clock = FakeClock()
    ppk2 = FakePpk2(clock)
    slept = []
    pb.capture(ppk2, tmp_path, seconds=0.1, voltage=5.0, warmup_s=120.0, clock=clock,
               sleep=slept.append)
    assert ppk2.calls == ["modifiers", "source", "voltage 5000", "power ON", "start", "stop"]
    assert slept == [120.0]
