import json
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ppk2_report as pr  # noqa: E402

RATE = 10_000  # samples per second in the synthetic traces


def pulses(seconds=10.0, base_ma=40.0, pulse_ma=140.0, every_s=1.0, width_s=0.1):
    """A square pulse train: base current with one pulse every @p every_s. @return t (s), I (uA)."""
    t = np.arange(int(seconds * RATE)) / RATE
    current = np.full(t.size, base_ma)
    current[(t % every_s) < width_s] = pulse_ma
    return t, current * 1000.0


def write_app_csv(path, t, current_ua, header="Timestamp(ms),Current(uA),D0-D7"):
    with open(path, "w") as out:
        out.write(header + "\n")
        digital = header.count(",") == 2
        for ts, value in zip(t, current_ua):
            out.write(f"{ts * 1000:.3f},{value:.3f}" + (",00000000\n" if digital else "\n"))


def test_app_csv_charge_and_bursts(tmp_path):
    t, current = pulses()
    path = tmp_path / "2026-10-13_COM3_E6_5v0.csv"
    write_app_csv(path, t, current)
    result = pr.analyze(pr.read_trace(path), voltage=5.0, threshold_ma=30.0)
    # 10 s at 40 mA plus 10 pulses of 0.1 s at +100 mA = 500 mA*s
    assert result["charge_mah"] == pytest.approx(500.0 / 3600, rel=0.005)
    assert result["avg_ma"] == pytest.approx(50.0, rel=0.005)
    assert result["avg_mw"] == pytest.approx(250.0, rel=0.005)
    assert result["peak_ma"] == pytest.approx(140.0)
    assert result["baseline_ma"] == pytest.approx(40.0)
    assert result["bursts"]["count"] == 10
    assert result["bursts"]["excess_mas"] == pytest.approx(100.0, rel=0.01)


def test_chunks_that_split_a_bin_give_the_same_result(tmp_path):
    t, current = pulses(seconds=2.0)
    path = tmp_path / "trace.csv"
    write_app_csv(path, t, current)
    whole = pr.read_trace(path)
    chunked = pr.read_trace(path, chunksize=777)
    np.testing.assert_allclose(chunked.mean_ua, whole.mean_ua)
    np.testing.assert_allclose(chunked.max_ua, whole.max_ua)


def test_header_units_are_converted(tmp_path):
    t, current = pulses(seconds=1.0)
    path = tmp_path / "trace.csv"
    with open(path, "w") as out:
        out.write("Timestamp(us),Current(mA)\n")
        for ts, value in zip(t, current):
            out.write(f"{ts * 1e6:.1f},{value / 1000:.6f}\n")
    result = pr.analyze(pr.read_trace(path), voltage=5.0)
    assert result["duration_s"] == pytest.approx(1.0, abs=0.002)
    assert result["avg_ma"] == pytest.approx(50.0, rel=0.005)


def test_bench_capture_round_trip(tmp_path):
    trace = pr.Trace(bin_s=0.001, mean_ua=np.array([1000.0, 3000.0] * 500),
                     max_ua=np.array([1500.0, 9000.0] * 500), epoch=1760000000.0)
    path = tmp_path / "trace.csv"
    pr.write_capture(path, trace, voltage=5.0)
    back = pr.read_trace(path)
    assert back.epoch == pytest.approx(1760000000.0)
    assert back.voltage == pytest.approx(5.0)
    np.testing.assert_allclose(back.mean_ua, trace.mean_ua)
    result = pr.analyze(back)
    assert result["avg_ma"] == pytest.approx(2.0)
    assert result["peak_ma"] == pytest.approx(9.0)


def test_window_selects_part_of_the_trace(tmp_path):
    t, current = pulses(seconds=10.0)
    current[t < 5.0] = 200_000.0  # boot burst in the first 5 s
    path = tmp_path / "trace.csv"
    write_app_csv(path, t, current, header="Timestamp(ms),Current(uA)")
    result = pr.analyze(pr.read_trace(path), voltage=5.0, start=5.0)
    assert result["duration_s"] == pytest.approx(5.0, abs=0.002)
    assert result["avg_ma"] == pytest.approx(50.0, rel=0.005)


def test_superframe_period_and_profile():
    period = 40.9
    bin_s = 0.01
    t = np.arange(int(600 / bin_s)) * bin_s
    current = np.full(t.size, 40_000.0)
    current[(t % period) < 0.4] = 130_000.0  # own TX slot
    current[((t - 12.0) % period) < 1.5] = 52_000.0  # RX slot
    trace = pr.Trace(bin_s=bin_s, mean_ua=current, max_ua=current)
    found, profile = pr.fold(trace, min_s=20.0, max_s=120.0)
    assert found == pytest.approx(period, abs=0.1)
    assert profile.max() == pytest.approx(130.0, rel=0.1)
    assert np.median(profile) == pytest.approx(40.0, rel=0.01)


def test_battery_life():
    life = pr.battery_life(avg_mw=200.0, cell_mah=3000, cell_v=3.7, usable=0.8)
    assert life["hours"] == pytest.approx(3000 * 3.7 * 0.8 / 200.0)
    scaled = pr.battery_life(avg_mw=200.0, cell_mah=3000, cell_v=3.7, usable=0.8, ratio=0.9)
    assert scaled["hours"] == pytest.approx(3000 * 3.7 * 0.8 / 180.0)


def test_voltage_from_the_file_name():
    assert pr.voltage_from_name("2026-10-13_COM3_E3_5v0.csv") == pytest.approx(5.0)
    assert pr.voltage_from_name("COM9_E3-BAT_3v8.csv") == pytest.approx(3.8)
    assert pr.voltage_from_name("trace.csv") is None


def test_cli_writes_json_and_plot(tmp_path):
    t, current = pulses(seconds=3.0)
    path = tmp_path / "2026-10-13_COM3_E0_5v0.csv"
    write_app_csv(path, t, current)
    assert pr.main([str(path), "--out", str(tmp_path), "--period", "none"]) == 0
    result = json.loads((tmp_path / "2026-10-13_COM3_E0_5v0.json").read_text())
    assert result["voltage"] == pytest.approx(5.0)
    assert result["avg_ma"] == pytest.approx(50.0, rel=0.005)
    assert (tmp_path / "2026-10-13_COM3_E0_5v0.png").stat().st_size > 0


def test_app_session_raw(tmp_path):
    t, current = pulses(seconds=2.0)
    samples = np.repeat(current, 100_000 // RATE)  # the app records 100 kS/s
    records = np.zeros(samples.size, dtype=[("ua", "<f4"), ("bits", "<u2")])
    records["ua"] = samples
    records["bits"] = 0x5555
    folder = tmp_path / "2b5bc18e"
    folder.mkdir()
    records.tofile(folder / "session.raw")
    result = pr.analyze(pr.read_trace(folder / "session.raw", chunksize=33_333), voltage=5.0)
    assert result["duration_s"] == pytest.approx(2.0)
    assert result["avg_ma"] == pytest.approx(50.0, rel=0.005)
    assert pr.read_trace(folder).mean_ua.size == 2000
