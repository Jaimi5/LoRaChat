"""Closed-form model of the LoRaMesher v2 TDMA superframe.

The superframe is deterministic given (node_count, max_hops, data_slots,
duty_cycle, slot_duration). This module reproduces the firmware's own slot
budget so the whole (SF x data_slots x duty_cycle) design space can be swept
analytically — experiments only need to validate a few anchor points.

Everything here mirrors LoRaMesher v2:
  - Time-on-Air: the standard LoRa airtime formula. Validated against the
    firmware-logged `ToA(N)` values (CRC on, explicit header, low-data-rate
    optimisation OFF, CR=4/7) — it reproduces ToA(242)=523, ToA(115)=829,
    ToA(51)=2728 ms to the millisecond.
  - Slot duration: `roundup50(ToA(max_packet) + guard + margin)`
    (network_service.cpp CalculateMinSlotDuration).
  - Superframe size: `max(duty_driven, kMinSlots, active+churn)` with the
    `max_network_nodes` pool capping total data slots
    (network_service.cpp:2156-2218, ShouldAcceptJoin:2807).

Energy reuses the per-state mA table from duty_cycle.py, so a modelled energy
value is the same method the pipeline already ships — not a shortcut.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

# ── Firmware constants (LoRaMesher v2) ───────────────────────────────────────
K_MIN_SLOTS = 16            # network_service.hpp:51
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

# Per-state currents (mA) — identical to analysis/duty_cycle.py CURRENT_MA.
CURRENT_MA = {"TX": 120.0, "RX": 12.0, "SLEEP": 1.0}
VOLTAGE_V = 3.3


def max_packet_for_sf(sf: int, bw_khz: float = 125.0) -> int:
    """RadioConfig::GetMaxPacketSizeForSf — SF-derived max payload (bytes)."""
    base = {7: 242, 8: 242, 9: 115, 10: 51, 11: 51, 12: 51}.get(sf, 51)
    scaled = base * 4 if bw_khz >= 499.9 else base * 2 if bw_khz >= 249.9 else base
    return max(1, min(255, scaled))


def toa_ms(sf: int, payload: int, bw_khz: float = 125.0, cr_denom: int = 7,
           preamble: int = 8, crc: bool = True, explicit_header: bool = True,
           ldro: bool = False) -> float:
    """LoRa time-on-air in ms. Matches the firmware's RadioLib getTimeOnAir."""
    bw = bw_khz * 1000.0
    t_sym = (2 ** sf) / bw                      # seconds
    t_preamble = (preamble + 4.25) * t_sym
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
    bound_by: str        # which term set total_slots: duty | kmin | active+churn
    data_slots_granted: float  # mean data slots actually granted per node


def superframe(sf: int, node_count: int, data_slots: int, duty_cycle: float,
               max_hops: int = 5, bw_khz: float = 125.0, cr_denom: int = 7,
               churn: int = DEFAULT_CHURN_MARGIN,
               max_data_slots: int = DEFAULT_MAX_DATA_SLOTS,
               guard_ms: int = DEFAULT_GUARD_MS,
               max_packet: int | None = None) -> Superframe:
    """Reproduce the firmware superframe slot budget for one config."""
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

    total = max(duty_driven, K_MIN_SLOTS, active_plus_margin)
    total = min(total, 255)
    if total == duty_driven and duty_driven >= max(K_MIN_SLOTS, active_plus_margin):
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
            **kw) -> dict:
    """Derived per-config metrics for the design-space figures.

    capacity_Bps      : mean per-node application throughput ceiling
    overhead_frac     : control+discovery+sync airtime / superframe airtime
    recurrence_s      : interval between a node's own data slots (~latency bound)
    mean_current_mA   : radio-on energy proxy (duty_cycle.py mA table)
    superframe_s      : wall-clock superframe duration
    """
    sframe = superframe(sf, node_count, data_slots, duty_cycle, **kw)
    mp = kw.get("max_packet") or max_packet_for_sf(sf, kw.get("bw_khz", 125.0))
    superframe_s = sframe.total_slots * sframe.slot_dur_ms / 1000.0

    per_node_data = sframe.data_slots_granted
    capacity_Bps = (per_node_data * mp) / superframe_s if superframe_s else 0.0
    overhead_slots = sframe.sync + sframe.control + sframe.discovery
    overhead_frac = overhead_slots / sframe.total_slots if sframe.total_slots else 0.0
    recurrence_s = superframe_s / per_node_data if per_node_data else math.inf

    # Energy: a representative node TXs its data slots + 1 control, RXs the rest
    # of the active slots, sleeps the remainder (same simplification as
    # duty_cycle.py's airtime x mA-table model).
    tx = per_node_data + 1
    active = sframe.sync + sframe.control + sframe.discovery + sframe.data
    rx = max(active - tx, 0)
    sleep = sframe.total_slots - active
    mean_mA = (tx * CURRENT_MA["TX"] + rx * CURRENT_MA["RX"]
               + sleep * CURRENT_MA["SLEEP"]) / sframe.total_slots

    return {
        "sf": sf, "node_count": node_count, "data_slots": data_slots,
        "duty_cycle": duty_cycle, "slot_dur_ms": sframe.slot_dur_ms,
        "total_slots": sframe.total_slots, "superframe_s": superframe_s,
        "data_slots_granted": per_node_data, "bound_by": sframe.bound_by,
        "capacity_Bps": capacity_Bps, "overhead_frac": overhead_frac,
        "recurrence_s": recurrence_s, "mean_current_mA": mean_mA,
        "max_packet_bytes": mp,
        "composition": {"sync": sframe.sync, "control": sframe.control,
                        "discovery": sframe.discovery, "data": sframe.data,
                        "sleep": sframe.sleep},
    }


def _selftest() -> None:
    """Validate ToA against the three firmware-logged anchor values."""
    cases = [(7, 242, 523), (9, 115, 829), (12, 51, 2728)]
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


if __name__ == "__main__":
    _selftest()
