import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import slot_model as sm  # noqa: E402


@pytest.mark.parametrize("nodes,hops,fraction,active,total", [
    (2, 1, 0.0, 10, 16),     # bench, 2 nodes: 16 slots (minimum)
    (4, 1, 0.0, 14, 16),     # bench, 4 nodes: 16 slots
    (4, 1, 0.85, 14, 94),    # bench, gateway /sleep.fraction 85: 94 slots
    (4, 2, 0.85, 17, 114),   # bench beacon while a node was 2 hops away: 114 slots
    (3, 1, 0.85, 12, 81),    # bench, 3 nodes: 81, not 80 (single-precision division)
    (20, 1, 0.0, 46, 48),    # active + churn margin of 2
    (60, 1, 0.9, 106, 255),  # 50 nodes and 50 data slots at most; wire limit 255
])
def test_superframe_slots_follow_loramesher(nodes, hops, fraction, active, total):
    slots = sm.superframe_slots(sm.Network(nodes=nodes, hops=hops, sleep_fraction=fraction))
    assert (slots.active, slots.total) == (active, total)


BENCH = sm.Currents(awake_ma=21.9, listen_ma=34.5, light_ma=1.17, deep_ma=0.81,
                    tx_base_mas=20.0, tx_per_node_mas=9.0, tx_cap_mas=200.0, deep_wake_mas=44.0)


def test_no_sleep_average_weights_listening_and_idle_slots():
    network = sm.Network(nodes=4, sleep_fraction=0.85)
    expected = (14 * 0.95 * 34.5 + 80 * 0.95 * 21.9 + 56.0) / (94 * 0.95)
    assert sm.average_ma(network, "none", BENCH) == pytest.approx(expected)


def test_transmit_charge_grows_with_the_routing_table_up_to_a_cap():
    assert sm.tx_mas(sm.Network(nodes=2), BENCH) == pytest.approx(38.0)
    assert sm.tx_mas(sm.Network(nodes=50), BENCH) == pytest.approx(200.0)


def test_an_absent_node_leaves_its_slot_asleep():
    full = sm.Network(nodes=4, sleep_fraction=0.85)
    one_away = sm.Network(nodes=4, present=3, sleep_fraction=0.85)
    saved = 0.95 * (34.5 - 1.17) + 9.0
    assert (sm.average_ma(full, "light", BENCH) - sm.average_ma(one_away, "light", BENCH)) == \
        pytest.approx(saved / (94 * 0.95))


def test_deep_sleep_pays_a_wake_per_superframe_and_only_on_long_runs():
    long_run = sm.Network(nodes=4, sleep_fraction=0.85)
    light = sm.average_ma(long_run, "light", BENCH)
    deep = sm.average_ma(long_run, "deep", BENCH)
    assert deep - light == pytest.approx(
        ((0.81 - 1.17) * 80 * 0.95 + 44.0) / (94 * 0.95))
    short_run = sm.Network(nodes=4, sleep_fraction=0.0)  # 2 SLEEP slots: below the deep minimum
    assert sm.average_ma(short_run, "deep", BENCH) == pytest.approx(
        sm.average_ma(short_run, "light", BENCH))


def test_battery_days():
    assert sm.battery_days(10.0, voltage=4.5) == pytest.approx(3000 * 3.7 * 0.8 / 45.0 / 24)


@pytest.mark.parametrize("sf,size,measured_ms", [(9, 25, 255), (9, 31, 312), (9, 43, 370),
                                                 (9, 65, 513)])
def test_airtime_matches_the_bursts_measured_on_the_bench(sf, size, measured_ms):
    assert sm.airtime_ms(sf, size) == pytest.approx(measured_ms, abs=8)


def test_transmit_charge_scales_with_airtime():
    sf9 = sm.Network(nodes=4)
    sf7 = sm.Network(nodes=4, sf=7, slot_s=0.65)
    ratio = sm.airtime_ms(7, 43) / sm.airtime_ms(9, 43)
    assert sm.tx_mas(sf7, BENCH) == pytest.approx(sm.tx_mas(sf9, BENCH) * ratio)
