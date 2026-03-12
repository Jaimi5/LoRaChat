import os
import re
import glob
from collections import defaultdict

_ANSI_RE      = re.compile(r'\x1b\[[0-9;]*[mGKHF]')
_SEND_RE        = re.compile(r'PKT_TX dst=0x[0-9A-Fa-f]+ src=0x[0-9A-Fa-f]+ type=0x([0-9A-Fa-f]+) size=(\d+)')
_RT_CREATE_RE   = re.compile(r'Created routing table message.*entry count: (\d+)')
_DEVICE_ADDR_RE = re.compile(r'\[0x([0-9A-Fa-f]+)\]')
_PKT_TX_RE = re.compile(
    r'\[0x([0-9A-Fa-f]+)\] PKT_TX dst=0x([0-9A-Fa-f]+) src=0x([0-9A-Fa-f]+) '
    r'type=0x([0-9A-Fa-f]+) size=(\d+)')
_PKT_RX_RE = re.compile(
    r'\[0x([0-9A-Fa-f]+)\] PKT_RX src=0x([0-9A-Fa-f]+) dst=0x([0-9A-Fa-f]+) '
    r'type=0x([0-9A-Fa-f]+) size=(\d+) rssi=(-?\d+) snr=(-?\d+)')
_SF_START_RE  = re.compile(r'Started superframe #(\d+)')
_SLOT_RE      = re.compile(r'Slot \d+ transition: type=(\w+)')

_TYPE_ROUTE_TABLE   = 0x32
_TYPE_SYNC_BEACON   = 0x46
_TYPE_JOIN_REQUEST  = 0x42
_TYPE_JOIN_RESPONSE = 0x43

# TODO: confirmed from logs: PKT_TX size=66 − payload_size=58 = 8 bytes (was wrong: 9)
_DATA_HEADER_BYTES   = 8
_SYNC_BEACON_BYTES   = 22
_JOIN_REQUEST_BYTES  = 14
_JOIN_RESPONSE_BYTES = 15
_OTHER_BYTES         = 18


def get_v2_overhead_data(monitoring_path) -> dict:
    """
    Parse all monitor_COM*.ans files in monitoring_path and return aggregate byte counts.

    Returns a dict with data/control byte counts, slot distributions, and derived metrics.
    """
    data_transmissions  = 0
    data_payload_bytes  = 0
    data_header_bytes   = 0
    route_table_count   = 0
    route_table_bytes   = 0
    sync_beacon_count   = 0
    sync_beacon_bytes   = 0
    join_request_count  = 0
    join_request_bytes  = 0
    join_response_count = 0
    join_response_bytes = 0
    other_msg_count     = 0
    other_msg_bytes     = 0
    total_superframes   = 0
    slot_counts         = {}

    pattern = os.path.join(monitoring_path, 'monitor_COM*.ans')
    files = glob.glob(pattern)

    for filepath in files:
        pending_rt_entries = None
        max_sf = 0

        with open(filepath, 'r', errors='replace') as f:
            for line in f:
                line = _ANSI_RE.sub('', line)

                sf_m = _SF_START_RE.search(line)
                if sf_m:
                    sf_num = int(sf_m.group(1))
                    if sf_num > max_sf:
                        max_sf = sf_num
                    continue

                slot_m = _SLOT_RE.search(line)
                if slot_m:
                    slot_type = slot_m.group(1)
                    slot_counts[slot_type] = slot_counts.get(slot_type, 0) + 1
                    continue

                rt_m = _RT_CREATE_RE.search(line)
                if rt_m:
                    pending_rt_entries = int(rt_m.group(1))
                    continue

                send_m = _SEND_RE.search(line)
                if send_m:
                    msg_type = int(send_m.group(1), 16)
                    size = int(send_m.group(2))
                    if msg_type == _TYPE_ROUTE_TABLE:
                        route_table_count += 1
                        route_table_bytes += size
                        pending_rt_entries = None
                    elif msg_type == _TYPE_SYNC_BEACON:
                        sync_beacon_count += 1
                        sync_beacon_bytes += size
                    elif msg_type == _TYPE_JOIN_REQUEST:
                        join_request_count += 1
                        join_request_bytes += size
                    elif msg_type == _TYPE_JOIN_RESPONSE:
                        join_response_count += 1
                        join_response_bytes += size
                    elif msg_type == 0x11:  # DATA
                        data_transmissions += 1
                        data_header_bytes  += _DATA_HEADER_BYTES
                        data_payload_bytes += max(0, size - _DATA_HEADER_BYTES)
                    else:
                        other_msg_count += 1
                        other_msg_bytes += size
                    continue

        total_superframes += max_sf

    join_bytes = join_request_bytes + join_response_bytes
    join_count = join_request_count + join_response_count

    total_bytes = (
        data_payload_bytes + data_header_bytes
        + route_table_bytes + sync_beacon_bytes
        + join_bytes + other_msg_bytes
    )

    overhead_pct = 0.0
    if total_bytes > 0:
        overhead_pct = round((total_bytes - data_payload_bytes) / total_bytes * 100, 2)

    return {
        'data_transmissions':    data_transmissions,
        'data_payload_bytes':    data_payload_bytes,
        'data_header_bytes':     data_header_bytes,
        'route_table_count':     route_table_count,
        'route_table_bytes':     route_table_bytes,
        'sync_beacon_count':     sync_beacon_count,
        'sync_beacon_bytes':     sync_beacon_bytes,
        'join_request_count':    join_request_count,
        'join_request_bytes':    join_request_bytes,
        'join_response_count':   join_response_count,
        'join_response_bytes':   join_response_bytes,
        'join_count':            join_count,
        'join_bytes':            join_bytes,
        'other_msg_count':       other_msg_count,
        'other_msg_bytes':       other_msg_bytes,
        'total_superframes':     total_superframes,
        'slot_counts':           slot_counts,
        'total_bytes':           total_bytes,
        'overhead_pct':          overhead_pct,
    }


def get_v2_per_superframe_data(monitoring_path) -> list:
    """
    Parse all monitor_COM*.ans files and return per-superframe aggregated data.

    Returns a list of dicts sorted by superframe number, with byte counts and
    overhead % per superframe (summed across all device log files).
    """
    sf_data = defaultdict(lambda: {
        'data_count':        0,
        'route_table_bytes': 0,
        'sync_beacon_bytes': 0,
        'join_bytes':        0,
        'other_bytes':       0,
        'data_payload_bytes': 0,
        'data_header_bytes': 0,
    })

    pattern = os.path.join(monitoring_path, 'monitor_COM*.ans')
    files = glob.glob(pattern)

    for filepath in files:
        current_sf = 0
        pending_rt_entries = None

        with open(filepath, 'r', errors='replace') as f:
            for line in f:
                line = _ANSI_RE.sub('', line)

                sf_m = _SF_START_RE.search(line)
                if sf_m:
                    current_sf = int(sf_m.group(1))
                    continue

                rt_m = _RT_CREATE_RE.search(line)
                if rt_m:
                    pending_rt_entries = int(rt_m.group(1))
                    continue

                send_m = _SEND_RE.search(line)
                if send_m:
                    msg_type = int(send_m.group(1), 16)
                    size = int(send_m.group(2))
                    if msg_type == _TYPE_ROUTE_TABLE:
                        sf_data[current_sf]['route_table_bytes'] += size
                        pending_rt_entries = None
                    elif msg_type == _TYPE_SYNC_BEACON:
                        sf_data[current_sf]['sync_beacon_bytes'] += size
                    elif msg_type in (_TYPE_JOIN_REQUEST, _TYPE_JOIN_RESPONSE):
                        sf_data[current_sf]['join_bytes'] += size
                    elif msg_type == 0x11:  # DATA
                        sf_data[current_sf]['data_count'] += 1
                        sf_data[current_sf]['data_header_bytes'] += _DATA_HEADER_BYTES
                        sf_data[current_sf]['data_payload_bytes'] += max(0, size - _DATA_HEADER_BYTES)
                    else:
                        sf_data[current_sf]['other_bytes'] += size
                    continue

    result = []
    for sf_num in sorted(sf_data.keys()):
        d = sf_data[sf_num]
        total = (
            d['data_payload_bytes'] + d['data_header_bytes']
            + d['route_table_bytes'] + d['sync_beacon_bytes']
            + d['join_bytes'] + d['other_bytes']
        )
        overhead_pct = 0.0
        if total > 0:
            overhead_pct = round((total - d['data_payload_bytes']) / total * 100, 2)
        result.append({
            'sf':                sf_num,
            'data_count':        d['data_count'],
            'route_table_bytes': d['route_table_bytes'],
            'sync_beacon_bytes': d['sync_beacon_bytes'],
            'join_bytes':        d['join_bytes'],
            'other_bytes':       d['other_bytes'],
            'data_payload_bytes': d['data_payload_bytes'],
            'data_header_bytes': d['data_header_bytes'],
            'total_bytes':       total,
            'overhead_pct':      overhead_pct,
        })

    return result


def get_v2_per_device_data(monitoring_path) -> list:
    """
    Parse all monitor_COM*.ans files and return per-device send/receive counts.

    'sent'     = PKT_TX lines where src == device_addr and type == DATA (0x11)
    'received' = PKT_RX lines where dst == device_addr and type == DATA (0x11)

    Returns a list of dicts sorted by device address.
    """
    trace = parse_packet_trace(monitoring_path)
    by_pair = trace['by_pair']

    # Collect all device addresses seen as src or dst
    all_devices = set()
    for src, dst in by_pair:
        all_devices.add(src)
        all_devices.add(dst)

    # Also include devices that appear only as relay/receiver with no sent DATA
    pattern = os.path.join(monitoring_path, 'monitor_COM*.ans')
    for filepath in glob.glob(pattern):
        device_addr = None
        with open(filepath, 'r', errors='replace') as f:
            for line in f:
                line = _ANSI_RE.sub('', line)
                if device_addr is None:
                    addr_m = _DEVICE_ADDR_RE.search(line)
                    if addr_m:
                        device_addr = addr_m.group(1).upper()
                        break
        if device_addr and device_addr.upper() != '0000':
            all_devices.add(device_addr)

    records = []
    for device in all_devices:
        sent      = sum(v['sent']     for (s, d), v in by_pair.items() if s == device)
        delivered = sum(v['received'] for (s, d), v in by_pair.items() if s == device)
        received  = sum(v['received'] for (s, d), v in by_pair.items() if d == device)
        reception_rate = round(delivered / sent * 100, 2) if sent > 0 else 0.0
        records.append({
            'device':         device,
            'sent':           sent,
            'delivered':      delivered,
            'received':       received,
            'reception_rate': reception_rate,
        })

    records.sort(key=lambda r: r['device'])
    return records


def parse_packet_trace(monitoring_path) -> dict:
    """
    Parse PKT_TX / PKT_RX logs to track application-level packet delivery.

    For each log file, determine the device address from the [0xADDR] prefix.
    Original sends:  PKT_TX where src == device_addr and type == DATA (0x11)
    Deliveries:      PKT_RX where dst == device_addr and type == DATA (0x11)

    Returns:
      {
        'by_pair': {(src, dst): {'sent': int, 'received': int, 'loss_pct': float}},
        'total_sent':     int,
        'total_received': int,
        'total_lost':     int,
        'loss_rate_pct':  float,
        'all_tx':  [(src, dst, type, size), ...],
        'all_rx':  [(src, dst, type, size, rssi, snr), ...],
      }
    """
    _DATA_TYPE = '11'  # hex string, matches type=0x11

    all_tx = []
    all_rx = []
    pair_sent     = defaultdict(int)
    pair_received = defaultdict(int)

    for filepath in glob.glob(os.path.join(monitoring_path, 'monitor_COM*.ans')):
        with open(filepath, 'r', errors='replace') as f:
            for line in f:
                line = _ANSI_RE.sub('', line)

                m = _PKT_TX_RE.search(line)
                if m:
                    logger = m.group(1).upper()  # device that logged the send
                    dst    = m.group(2).upper()
                    src    = m.group(3).upper()
                    typ    = m.group(4).upper()
                    size   = int(m.group(5))
                    all_tx.append((src, dst, typ, size))
                    if typ == _DATA_TYPE:
                        pair_sent[(logger, dst)] += 1
                    continue

                m = _PKT_RX_RE.search(line)
                if m:
                    logger = m.group(1).upper()  # device that logged the receive
                    src    = m.group(2).upper()
                    dst    = m.group(3).upper()
                    typ    = m.group(4).upper()
                    size   = int(m.group(5))
                    rssi   = int(m.group(6))
                    snr    = int(m.group(7))
                    all_rx.append((src, dst, typ, size, rssi, snr))
                    if typ == _DATA_TYPE and logger == dst:
                        # packet reached its intended destination
                        pair_received[(src, logger)] += 1

    by_pair = {}
    for pair, sent in pair_sent.items():
        received = pair_received.get(pair, 0)
        lost     = sent - received
        by_pair[pair] = {
            'sent':     sent,
            'received': received,
            'lost':     lost,
            'loss_pct': round(lost / sent * 100, 2) if sent > 0 else 0.0,
        }

    total_sent     = sum(v['sent']     for v in by_pair.values())
    total_received = sum(v['received'] for v in by_pair.values())
    total_lost     = total_sent - total_received

    return {
        'by_pair':        by_pair,
        'total_sent':     total_sent,
        'total_received': total_received,
        'total_lost':     total_lost,
        'loss_rate_pct':  round(total_lost / total_sent * 100, 2) if total_sent else 0.0,
        'all_tx':         all_tx,
        'all_rx':         all_rx,
    }


if __name__ == '__main__':
    import sys
    import json

    path = sys.argv[1] if len(sys.argv) > 1 else '.'
    result = get_v2_overhead_data(path)
    # slot_counts is not JSON-serializable as defaultdict, convert
    result['slot_counts'] = dict(result['slot_counts'])
    print(json.dumps(result, indent=2))
