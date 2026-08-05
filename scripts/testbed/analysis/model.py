"""Closed-form model of the LoRaMesher v2 TDMA superframe.

The superframe is deterministic given (node_count, max_hops, data_slots,
duty_cycle, slot_duration). This module reproduces the firmware's own slot
budget so the whole (SF x data_slots x duty_cycle) design space can be swept
analytically — experiments only need to validate a few anchor points.

Everything here mirrors LoRaMesher v2:
  - Time-on-Air: the standard LoRa airtime formula. Validated against the
    firmware-logged `ToA(N)` values (CRC on, explicit header, CR=4/7, low-data-
    rate optimisation derived from the symbol time) — it reproduces
    ToA(242)=523, ToA(115)=829, ToA(51)=3187 ms to the millisecond.
  - Slot duration: `roundup50(ToA(max_packet) + guard + margin)`
    (network_service.cpp CalculateMinSlotDuration).
  - Superframe size: `max(duty_driven, kMinSlots, active+churn)` with the
    `max_network_nodes` pool capping total data slots
    (network_service.cpp:2156-2218, ShouldAcceptJoin:2807).

Energy uses the per-state mA table from analysis/radio_power.py — the same
table duty_cycle.py applies to measured slot logs, so a modelled energy value is
the same method the pipeline already ships, not a shortcut.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

# Allow direct script invocation from any cwd:
#   python3 scripts/testbed/analysis/model.py
if __name__ == "__main__":
    import sys
    from pathlib import Path
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.radio_power import DEFAULT_TX_DBM, VOLTAGE_V, current_table

# ── Firmware constants (LoRaMesher v2) ───────────────────────────────────────
K_MIN_SLOTS = 16            # network_service.hpp:51
MAX_SLOTS = 255             # The sync beacon advertises the superframe size in a
                            # uint8_t wire field (sync_beacon_header.hpp), so the
                            # frame cannot grow past 255 slots. This is a
                            # WIRE-FORMAT choice, not a protocol or radio limit:
                            # it is what floors the achievable mean current at low
                            # duty / high sleep targets. Pass max_slots=65535 to
                            # model what a uint16_t field would allow — mean
                            # current then tends to the sleep current instead.
MAX_SLOTS_UINT16 = 65535    # Hypothetical: total_slots widened to uint16_t.
DEFAULT_CHURN_MARGIN = 2    # protocol_configuration.hpp (churn_margin_slots_)
DEFAULT_MAX_DATA_SLOTS = 50  # protocol_configuration.hpp (max_data_slots_) — the
                            # data-slot pool, independent of the node cap since
                            # LoRaMesher 4e64315.
DEFAULT_GUARD_MS = 50       # protocol_configuration.hpp (guard_time_ms_)
PROCESSING_MARGIN_MS = 50   # network_service.cpp kProcessingMarginMs
SLOT_ROUND_MS = 50          # network_service.cpp kRoundMs

# Header sizes feeding the NM TX-time (bytes). AddressType=2, MessageType=1.
_BASE_HEADER = 6                         # base_header.hpp Size()
_SYNC_SIZE = _BASE_HEADER + 14           # sync_beacon_header.hpp SyncBeaconFieldsSize()=14
_RT_FIXED = _BASE_HEADER + 6             # routing_table_header.hpp RoutingTableFieldsSize()=6
_RT_ENTRY = 10                           # routing_table_entry.hpp Size()

def max_packet_for_sf(sf: int, bw_khz: float = 125.0) -> int:
    """RadioConfig::GetMaxPacketSizeForSf — SF-derived max payload (bytes)."""
    base = {7: 242, 8: 242, 9: 115, 10: 51, 11: 51, 12: 51}.get(sf, 51)
    scaled = base * 4 if bw_khz >= 499.9 else base * 2 if bw_khz >= 249.9 else base
    return max(1, min(255, scaled))


def should_enable_ldro(sf: int, bw_khz: float = 125.0) -> bool:
    """Low-data-rate optimisation is on when the symbol time reaches 16 ms.

    Mirrors `airtime::ShouldEnableLdro` (src/utils/lora_airtime.hpp) and
    RadioLib's automatic behaviour, i.e. on at SF11/SF12 with BW125.
    """
    if bw_khz <= 0.0:
        return False
    return ((2 ** sf) / bw_khz) >= 16.0


def toa_ms(sf: int, payload: int, bw_khz: float = 125.0, cr_denom: int = 7,
           preamble: int = 8, crc: bool = True, explicit_header: bool = True,
           ldro: bool | None = None) -> float:
    """LoRa time-on-air in ms. Matches the firmware's RadioLib getTimeOnAir.

    `ldro=None` derives the setting from the symbol time, as the radio does.
    """
    bw = bw_khz * 1000.0
    t_sym = (2 ** sf) / bw                      # seconds
    t_preamble = (preamble + 4.25) * t_sym
    if ldro is None:
        ldro = should_enable_ldro(sf, bw_khz)
    de = 1 if ldro else 0
    cr = cr_denom - 4                            # 4/7 -> 3
    ih = 0 if explicit_header else 1
    num = 8 * payload - 4 * sf + 28 + (16 if crc else 0) - 20 * ih
    den = 4 * (sf - 2 * de)
    payload_symb = 8 + max(math.ceil(num / den) * (cr + 4), 0)
    return (t_preamble + payload_symb * t_sym) * 1000.0


def slot_duration_ms(sf: int, bw_khz: float = 125.0, cr_denom: int = 7,
                     guard_ms: int = DEFAULT_GUARD_MS,
                     max_packet: int | None = None) -> int:
    """roundup50(ToA(max_packet) + guard + margin) — CalculateMinSlotDuration."""
    mp = max_packet if max_packet is not None else max_packet_for_sf(sf, bw_khz)
    raw = toa_ms(sf, mp, bw_khz, cr_denom) + guard_ms + PROCESSING_MARGIN_MS
    return int(math.ceil(raw / SLOT_ROUND_MS) * SLOT_ROUND_MS)


def _nm_tx_time_ms(sf: int, node_count: int, nm_data_slots: int,
                   bw_khz: float, cr_denom: int, max_packet: int) -> float:
    """CalculateNMTxTimeMs: ToA(sync) + ToA(routing table) + data * ToA(maxpkt)."""
    rt_entries = max(node_count - 1, 0)
    rt_size = min(_RT_FIXED + rt_entries * _RT_ENTRY, 255)
    return (toa_ms(sf, _SYNC_SIZE, bw_khz, cr_denom)
            + toa_ms(sf, rt_size, bw_khz, cr_denom)
            + nm_data_slots * toa_ms(sf, max_packet, bw_khz, cr_denom))


@dataclass
class Superframe:
    sf: int
    total_slots: int
    sync: int
    control: int
    discovery: int
    data: int            # total data slots across all nodes (after pool cap)
    sleep: int
    slot_dur_ms: int
    bound_by: str        # which term set total_slots:
                         # duty | kmin | active+churn | min-sleep | slot-index cap
    data_slots_granted: float  # mean data slots actually granted per node


def superframe(sf: int, node_count: int, data_slots: int, duty_cycle: float,
               max_hops: int = 5, bw_khz: float = 125.0, cr_denom: int = 7,
               churn: int = DEFAULT_CHURN_MARGIN,
               max_data_slots: int = DEFAULT_MAX_DATA_SLOTS,
               guard_ms: int = DEFAULT_GUARD_MS,
               max_packet: int | None = None,
               min_sleep_fraction: float = 0.0,
               max_slots: int = MAX_SLOTS) -> Superframe:
    """Reproduce the firmware superframe slot budget for one config.

    `min_sleep_fraction` forces a sleep floor by padding the frame, independently
    of the duty target. It is the only knob that takes v2 below v1's always-on RX
    current — the duty target is inert once the frame is active-bound. The testbed
    ran it at 0 (`config.h` LORA_MIN_SLEEP_FRACTION).

    `max_slots` is the wire-field ceiling. Default 255 (the shipped uint8_t);
    MAX_SLOTS_UINT16 models a widened field.
    """
    mp = max_packet if max_packet is not None else max_packet_for_sf(sf, bw_khz)
    slot_dur = slot_duration_ms(sf, bw_khz, cr_denom, guard_ms, mp)

    # Data-slot pool: max_data_slots caps the SUM of per-node data slots
    # (ShouldAcceptJoin / ProcessSlotRequest). Granted per node is
    # min(requested, what the pool left).
    requested_total = node_count * data_slots
    total_data = min(requested_total, max_data_slots)
    granted_per_node = total_data / node_count if node_count else 0.0

    sync = max_hops + 1
    control = node_count
    discovery = (max_hops + 1) * 2
    active = sync + control + discovery + total_data

    nm_data = min(data_slots, max(1, max_data_slots - (node_count - 1) * data_slots)) \
        if requested_total > max_data_slots else data_slots
    nm_data = max(nm_data, 1)
    tx_time = _nm_tx_time_ms(sf, node_count, nm_data, bw_khz, cr_denom, mp)
    duty_driven = math.ceil(tx_time / (slot_dur * duty_cycle)) if duty_cycle > 0 else 0
    active_plus_margin = active + churn

    # Pad the frame to guarantee a sleep fraction. Mirrors the firmware exactly:
    # min_for_sleep INITIALISES to the active count (not 0), and the guard is
    # strict on both sides — so min_sleep_fraction == 1.0 is a silent NO-OP that
    # falls back to minimum sleep rather than maximum. That is a firmware quirk,
    # not a bug to fix here: this model's job is to match what ships.
    # (new_loramesher: NetworkService slot-budget block; the same code lives in
    # SlotScheduler on refactor/architecture-review. Cite the branch, not a line
    # number — it has moved three times.)
    min_for_sleep = want_for_sleep = active
    if 0.0 < min_sleep_fraction < 1.0:
        want_for_sleep = math.ceil(active / (1.0 - min_sleep_fraction))
        min_for_sleep = min(want_for_sleep, max_slots)

    total = max(duty_driven, K_MIN_SLOTS, min_for_sleep, active_plus_margin)
    # The firmware clamps min_for_sleep to the wire limit *before* the max(), so
    # `total` alone cannot reveal a binding cap. Compare the UNCLAMPED request, or
    # a sleep-capped frame mislabels itself as "min-sleep".
    uncapped = max(duty_driven, K_MIN_SLOTS, want_for_sleep, active_plus_margin)
    total = min(total, max_slots)
    if uncapped > max_slots:
        # The wire field, not any budget term, set the size. This is what floors
        # the achievable mean current: the active slots keep their share of a
        # frame that cannot grow further, however deep the sleep target goes.
        bound = "slot-index cap"
    elif total == min_for_sleep and min_for_sleep > max(duty_driven, K_MIN_SLOTS,
                                                       active_plus_margin):
        bound = "min-sleep"
    elif total == duty_driven and duty_driven >= max(K_MIN_SLOTS, active_plus_margin):
        bound = "duty"
    elif total == active_plus_margin and active_plus_margin >= K_MIN_SLOTS:
        bound = "active+churn"
    else:
        bound = "kmin"

    return Superframe(sf=sf, total_slots=int(total), sync=sync, control=control,
                      discovery=discovery, data=int(total_data),
                      sleep=int(total - active), slot_dur_ms=slot_dur,
                      bound_by=bound, data_slots_granted=granted_per_node)


def metrics(sf: int, node_count: int, data_slots: int, duty_cycle: float,
            *, tx_power_dbm: float = DEFAULT_TX_DBM, source: str = "best",
            i_rx_override: float | None = None, **kw) -> dict:
    """Derived per-config metrics for the design-space figures.

    capacity_Bps      : mean per-node application throughput ceiling
    overhead_frac     : control+discovery+sync airtime / superframe airtime
    recurrence_s      : interval between a node's own data slots (~latency bound)
    mean_current_mA   : radio-on energy proxy — an UPPER BOUND, see `representative`
    representative    : which node this describes ("worst_case_relay")
    superframe_s      : wall-clock superframe duration

    `mean_current_mA` is the current of a node that never sleeps through the data
    band. Real nodes do, by 5-32% depending on network size — see the note in the
    energy block below. Quote it as a bound, never as a prediction.

    `tx_power_dbm` defaults to the firmware's compiled-in LORA_POWER rather than
    any one campaign's per-node override: this is the design-space tool, so it
    should describe the shipped configuration. `i_rx_override` forces the RX
    current, for sweeping the sensitivity that actually decides v1-vs-v2. Both
    keyword-only, so neither leaks into superframe()'s **kw.

    NOTE: `min_sleep_fraction` and `max_slots` reach superframe() through **kw.
    """
    sframe = superframe(sf, node_count, data_slots, duty_cycle, **kw)
    mp = kw.get("max_packet") or max_packet_for_sf(sf, kw.get("bw_khz", 125.0))
    superframe_s = sframe.total_slots * sframe.slot_dur_ms / 1000.0

    per_node_data = sframe.data_slots_granted
    capacity_Bps = (per_node_data * mp) / superframe_s if superframe_s else 0.0
    overhead_slots = sframe.sync + sframe.control + sframe.discovery
    overhead_frac = overhead_slots / sframe.total_slots if sframe.total_slots else 0.0
    recurrence_s = superframe_s / per_node_data if per_node_data else math.inf

    # Energy: a representative node TXs its data slots + 1 control, RXs EVERY
    # other active slot, and sleeps only what the frame budget leaves over.
    #
    # That RX assumption is a WORST CASE, not a central estimate. A real node
    # sleeps through the data slots of nodes it does not relay for, and the data
    # band is most of the frame. Measured against this model (2 dBm nodes,
    # duty_cycle.py slot integration):
    #
    #   13 nodes, ds=1  sleep 18.8% real vs 5.4% here  ->  12.22 mA vs 12.78  (+5%)
    #   13 nodes, ds=1  (main_cluster_pdr)             ->  11.95 mA vs 12.78  (+7%)
    #   16 nodes, ds=1  (formation)                    ->  10.64 mA vs 12.60 (+19%)
    #   16 nodes, ds=2  sleep 32.6% real vs 3.4% here  ->   9.76 mA vs 12.90 (+32%)
    #
    # The error grows with the data band (node_count x data_slots) and is biased
    # AGAINST v2. Deliberately not "fixed": how much a node relays depends on its
    # position in the tree, which this closed form does not represent, and a fitted
    # relay fraction would be a static average standing in for something that
    # measurably varies 6-56% across nodes. So this stays an upper bound and says
    # so. For any config actually run, use the measured number from duty_cycle.py
    # instead — it is exact and assumption-free.
    table = current_table(tx_power_dbm, source=source)
    if i_rx_override is not None:
        table["RX"] = i_rx_override
    tx = per_node_data + 1
    active = sframe.sync + sframe.control + sframe.discovery + sframe.data
    rx = max(active - tx, 0)
    sleep = sframe.total_slots - active
    mean_mA = (tx * table["TX"] + rx * table["RX"]
               + sleep * table["SLEEP"]) / sframe.total_slots

    return {
        "sf": sf, "node_count": node_count, "data_slots": data_slots,
        "duty_cycle": duty_cycle, "slot_dur_ms": sframe.slot_dur_ms,
        "total_slots": sframe.total_slots, "superframe_s": superframe_s,
        "data_slots_granted": per_node_data, "bound_by": sframe.bound_by,
        "capacity_Bps": capacity_Bps, "overhead_frac": overhead_frac,
        "recurrence_s": recurrence_s, "mean_current_mA": mean_mA,
        # Names the node mean_current_mA describes, so a figure or doc cannot
        # present this upper bound as a central estimate without saying so.
        "representative": "worst_case_relay",
        "max_packet_bytes": mp,
        "composition": {"sync": sframe.sync, "control": sframe.control,
                        "discovery": sframe.discovery, "data": sframe.data,
                        "sleep": sframe.sleep},
    }


def convergence_floor_s(sf: int, node_count: int, data_slots: int,
                        duty_cycle: float, joins_per_sf: int = 3,
                        max_hops: int = 5, couple_depth: bool = True,
                        **kw) -> float:
    """Optimal cold-start convergence floor (seconds), join-admission limited.

    Two independent necessary conditions gate a cold network reaching full
    routing:

      - Admission rate. The Network Manager admits at most kMaxPendingJoins (=3)
        new nodes per superframe — they are buffered in `pending_joins_` and
        drained as a batch at the superframe boundary in `ApplyPendingJoin`
        (network_service.hpp:1205, network_service.cpp). With the root already
        present, the remaining N-1 nodes need `ceil((N-1)/joins_per_sf)`
        superframes just to clear that queue.
      - Depth. A depth-D node cannot join before its parent has joined and is
        beaconing, so the tree grows at most one level per superframe and needs
        `>= max_hops` superframes to populate the deepest leaf.

    Convergence cannot beat either, so the floor is the larger:

        couple_depth=True : T = max(D, ceil((N-1)/k)) * superframe_duration(SF)
        couple_depth=False: T =        ceil((N-1)/k)  * superframe_duration(SF)

    `k=3` is the firmware best case; `k=1` is a hypothetical fully-serialized
    worst case (not a firmware mode). This is an *optimistic* lower bound: it is
    built on the converged superframe (early frames are shorter), ignores
    discovery contention, and bounds *admission* — all-pairs route convergence
    lags admission completion by >= 1 further superframe. Measured convergence
    must exceed it.
    """
    s = superframe(sf, node_count, data_slots, duty_cycle, max_hops=max_hops, **kw)
    superframe_s = s.total_slots * s.slot_dur_ms / 1000.0
    join_superframes = math.ceil(max(node_count - 1, 0) / max(joins_per_sf, 1))
    n_sf = max(max_hops, join_superframes) if couple_depth else join_superframes
    return n_sf * superframe_s


def _selftest_sleep() -> None:
    """Pin the min_sleep_fraction term against the firmware's own quirks."""
    from analysis.radio_power import SLEEP_MA

    # 1. Inert at the default. The campaign ran msf=0, so every pre-existing
    #    number must be untouched by the new term.
    for sf in (7, 9, 12):
        for ds in (1, 2, 4):
            a = superframe(sf, 13, ds, 0.1, max_hops=2)
            b = superframe(sf, 13, ds, 0.1, max_hops=2, min_sleep_fraction=0.0)
            assert a == b, f"msf=0 perturbed the budget at SF{sf} ds={ds}"

    # 2. msf == 1.0 is a silent no-op (the firmware guard is 0 < msf < 1), so it
    #    falls back to MINIMUM sleep, not maximum. Counter-intuitive by design.
    assert (superframe(9, 13, 1, 0.1, max_hops=2, min_sleep_fraction=1.0)
            == superframe(9, 13, 1, 0.1, max_hops=2)), "msf=1.0 must be a no-op"

    # 3. SF-invariance of mean current. msf fixes slot COMPOSITION; SF fixes only
    #    slot DURATION. The regime figure's x-axis depends on this holding.
    for msf in (0.0, 0.3, 0.5, 0.9):
        got = {metrics(sf, node_count=13, data_slots=1, duty_cycle=0.1, max_hops=2,
                       tx_power_dbm=2.0, min_sleep_fraction=msf)["mean_current_mA"]
               for sf in (7, 9, 12)}
        assert len(got) == 1, f"mean current not SF-invariant at msf={msf}: {got}"

    # 4. The 255-slot cap is a uint8_t wire field, not a floor: widen it and the
    #    mean current tends to the sleep current.
    deep = metrics(9, node_count=13, data_slots=1, duty_cycle=0.1, max_hops=2,
                   tx_power_dbm=2.0, min_sleep_fraction=0.99999,
                   max_slots=MAX_SLOTS_UINT16)["mean_current_mA"]
    assert abs(deep - SLEEP_MA) < 0.02, f"asymptote {deep} should approach {SLEEP_MA}"

    # 5. A sleep-capped frame must say so, not mislabel itself "min-sleep".
    capped = superframe(9, 13, 1, 0.1, max_hops=2, min_sleep_fraction=0.9)
    assert capped.total_slots == MAX_SLOTS
    assert capped.bound_by == "slot-index cap", capped.bound_by
    assert superframe(9, 13, 1, 0.1, max_hops=2,
                      min_sleep_fraction=0.5).bound_by == "min-sleep"

    print("sleep-term selftest: OK "
          "(msf=0 inert, msf=1.0 no-op, SF-invariant, uint16 -> sleep current)")


def _selftest() -> None:
    """Validate ToA against the three firmware-logged anchor values."""
    _selftest_sleep()
    cases = [(7, 242, 523), (9, 115, 829), (12, 51, 3187)]
    print("ToA validation (firmware-logged vs model):")
    for sf, payload, logged in cases:
        got = toa_ms(sf, payload)
        slot = slot_duration_ms(sf)
        print(f"  SF{sf:<2} ToA({payload})  logged={logged:>5} ms  "
              f"model={got:7.1f} ms  diff={got - logged:+5.1f}  slot_dur={slot} ms")
    print("\nSuperframe examples (16-node-ish cluster):")
    for sf in (7, 9, 12):
        for ds in (1, 2, 4):
            m = metrics(sf, node_count=13, data_slots=ds, duty_cycle=0.1, max_hops=2)
            print(f"  SF{sf:<2} ds={ds}: total={m['total_slots']:>3} slots "
                  f"({m['superframe_s']:5.1f}s) bound={m['bound_by']:<12} "
                  f"cap={m['capacity_Bps']:5.2f} B/s  oh={m['overhead_frac']*100:4.1f}% "
                  f"I={m['mean_current_mA']:5.1f}mA")

    print("\nJoin-admission convergence floor (formation: 16 nodes, duty=0.1, D=5):")
    print("  (k = joins/superframe; max = max(D,join), pure = join only)")
    for sf in (7, 9, 12):
        m = metrics(sf, node_count=16, data_slots=1, duty_cycle=0.1, max_hops=5)
        floors = {
            (k, cd): convergence_floor_s(sf, node_count=16, data_slots=1,
                                         duty_cycle=0.1, joins_per_sf=k,
                                         max_hops=5, couple_depth=cd)
            for k in (3, 1) for cd in (True, False)
        }
        print(f"  SF{sf:<2}: superframe={m['superframe_s']:6.1f}s  "
              f"k=3 max={floors[(3, True)]:7.1f}s pure={floors[(3, False)]:7.1f}s  "
              f"k=1 max={floors[(1, True)]:7.1f}s pure={floors[(1, False)]:7.1f}s")


if __name__ == "__main__":
    _selftest()
