#!/usr/bin/env python3
"""Per-node, per-day health report for a long default-firmware soak.

Reads one log directory holding, per node, any mix of
    monitor-dev-<session>-<ID>-YYYYMMDD.log.gz   day files cut by gw-monitor.sh rotate
    monitor-dev-<session>-<ID>.log[.gz]          the live file (or a snapshot of it)
and reports, per node and per (gateway-local) day:
  - boots and crash reboots (BOOT reason=, ROM `rst:0x`, Guru/abort markers),
  - monitor gaps (log silence > --gap-s, i.e. the monitor/USB, not the node),
    and monitor restarts written by the gw-monitor.sh restart loop,
  - joins, fault recoveries and NM elections from `Network service state changed`
    (these are the flaps formation.py's cumulative convergence cannot see),
  - % of time joined (state 3 NORMAL_OPERATION / 4 NETWORK_MANAGER), time weighted,
  - sync-beacon coverage: superframes (manager's own beacon TX) in which the node
    received at least one sync beacon (PKT_RX type 0x46, direct or forwarded),
  - long-link RSSI/SNR (006C<->3428 by default) and its beacon/routing-table RX,
  - monitor messages: `Sending DATA` per node and destination against `DATA reached
    final destination` at that destination (LoRaChat sends to the closest gateway, so
    every node that receives DATA is an uplink; there can be several),
  - HEALTH heap trend (least-squares slope, bytes/day) and minheap, STACK min free
    per task, WARNING/ERROR line templates.
The day a line belongs to is taken from its gateway timestamp, never from the
file name, so a day file's first minutes of the next day land on the right day.

Usage:
    python3 soak_report.py LOG_DIR [--json OUT.json] [--label soak_200_sf10]
    python3 soak_report.py ../../../LoRaChat/logs_testbed/full_deploy_sf10_v3 --json v3.json
"""

from __future__ import annotations

import argparse
import bisect
import json
import math
import re
import statistics
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from multiprocessing import Pool
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from parse_logs import _open_log, _strip_ansi, v2_runtime_addr  # noqa: E402

SUMMARY_VERSION = 1

_FILE_RE = re.compile(r"monitor-dev-.+?-([0-9A-Fa-f]{4})(?:-(\d{8}))?\.log(?:\.gz)?$")

RE_PKT_RX = re.compile(r"PKT_RX src=0x([0-9A-Fa-f]+) dst=0x([0-9A-Fa-f]+) type=0x([0-9A-Fa-f]+) "
                       r"size=\d+ rssi=(-?\d+\.?\d*) snr=(-?\d+\.?\d*)")
RE_PKT_TX = re.compile(r"PKT_TX dst=0x([0-9A-Fa-f]+) src=0x([0-9A-Fa-f]+) type=0x([0-9A-Fa-f]+)")
RE_STATE = re.compile(r"Network service state changed to (\d+)")
# The library prints its current state inside other lines ("*** RECEIVED MESSAGE ...
# (my state: 3, ...", "PROCESSING JOIN_REQUEST ... (state: 3, ...").
RE_STATE_SEEN = re.compile(r"\((?:my )?state: (\d+)[,)]")
RE_HEALTH = re.compile(r"HEALTH node=0x[0-9A-Fa-f]+ heap=(\d+) minheap=(\d+) role=(-?\d+) state=(-?\d+)")
RE_BOOT = re.compile(r"BOOT node=0x[0-9A-Fa-f]+ reason=(-?\d+)")
RE_RST = re.compile(r"rst:0x([0-9A-Fa-f]+)")
RE_STACK = re.compile(r"STACK\[([^\]]+)\] total=(\d+) free=(\d+)(?=\s|\x1b|$)")  # skip lines cut by other tasks
RE_SEND = re.compile(r"Sending DATA to 0x([0-9A-Fa-f]+) via 0x[0-9A-Fa-f]+ \(ttl=\d+, seq=(\d+)\)")
RE_DELIV = re.compile(r"DATA reached final destination: src=0x([0-9A-Fa-f]+), dest=0x([0-9A-Fa-f]+), seq=(\d+)")
RE_CFG = re.compile(r"\bCFG (node=0x[0-9A-Fa-f]+ .*)$")
RE_LIBLVL = re.compile(r"\[(WARNING|ERROR)\]\s*(?:\[0x[0-9A-Fa-f]+\]\s*)?(.*)$")
RE_ESPLVL = re.compile(r"^([WE]) \(\d+\) ([^:]+):\s?(.*)$")
RE_NORM = re.compile(r"0x[0-9A-Fa-f]+|\d+(?:\.\d+)?")

CRASH_MARKERS = ("Guru Meditation", "abort() was called", "Stack canary", "stack overflow",
                 "Backtrace:")
# esp_reset_reason(): 1 POWERON, 2 EXT, 3 SW, 4 PANIC, 5 INT_WDT, 6 TASK_WDT, 7 WDT,
# 8 DEEPSLEEP, 9 BROWNOUT. A power-on/external reset is the gateway's doing
# (upload, monitor DTR/RTS, USB power); the rest are the node's.
RESET_REASON = {0: "unknown", 1: "poweron", 2: "ext", 3: "sw", 4: "panic", 5: "int_wdt",
                6: "task_wdt", 7: "wdt", 8: "deepsleep", 9: "brownout", 10: "sdio"}
CRASH_REASONS = {3, 4, 5, 6, 7, 9}
# ROM boot line codes that are not a plain power-on: 0x3 SW, 0xc SW_CPU, 0x7/0x8 TG WDT,
# 0x9 RTCWDT_SYS, 0xf brownout, 0x10 RTCWDT_RTC (used when the BOOT line is absent).
CRASH_RST = {"3", "c", "7", "8", "9", "f", "10"}

JOINED_STATES = (3, 4)
STATE_FAULT_RECOVERY = 5
STATE_NM_ELECTION = 6
TYPE_SYNC_BEACON = 0x46
TYPE_ROUTE_TABLE = 0x32
BEACON_SKEW_S = 2.0       # tolerance for gateway clock skew when binning beacons
DAY_S = 86400
MIN_HEAP_FIT_DAYS = 0.5   # no heap slope from a shorter HEALTH span
IN_FLIGHT_S = 1200        # sends this close to the end of the data may still be in flight
UPLOAD_BOOT_GRACE_S = 120  # a boot this close to a node's first line is the upload's
BOOT_LINE_WINDOW_S = 30    # the app's BOOT line follows the ROM rst line within this


def _epoch(ts: str, _cache: dict = {}) -> float:
    """'YYYY-MM-DD HH:MM:SS.mmm' (gateway local, treated as UTC) -> epoch seconds."""
    key = ts[:16]
    base = _cache.get(key)
    if base is None:
        base = datetime.strptime(key, "%Y-%m-%d %H:%M").replace(tzinfo=timezone.utc).timestamp()
        _cache[key] = base
    try:
        return base + float(ts[17:23])
    except ValueError:
        return base


def _day(t: float) -> str:
    return datetime.fromtimestamp(t, tz=timezone.utc).strftime("%Y-%m-%d")


def node_files(log_dir: Path) -> dict[str, list[Path]]:
    """{label: [day files in date order, then the live file]}"""
    out: dict[str, list[tuple[str, Path]]] = defaultdict(list)
    for p in log_dir.glob("monitor-dev-*.log*"):
        m = _FILE_RE.search(p.name)
        if m:
            out[m.group(1).upper()].append((m.group(2) or "99999999", p))
    return {k: [p for _, p in sorted(v)] for k, v in sorted(out.items())}


class _Day:
    __slots__ = ("lines", "first", "last", "gaps", "gap_s", "mon_restarts", "boots", "crash_boots",
                 "boot_reasons", "crash_markers", "joins", "join_attempts", "join_timeouts",
                 "fault_recoveries", "nm_elections", "state_s", "joined_s", "health_n",
                 "health_joined", "heap_min", "heap_last", "minheap_last", "stack_min",
                 "beacon_rx", "mon_tx", "mon_tx_to", "delivered_from", "ll_rssi", "ll_snr",
                 "ll_beacons", "ll_rtables", "ll_rx", "warn", "pkt_tx")

    def __init__(self):
        self.lines = 0
        self.first = self.last = None
        self.gaps = 0
        self.gap_s = 0.0
        self.mon_restarts = 0
        self.boots = 0
        self.crash_boots = 0
        self.boot_reasons = Counter()
        self.crash_markers = 0
        self.joins = self.join_attempts = self.join_timeouts = 0
        self.fault_recoveries = self.nm_elections = 0
        self.state_s = 0.0
        self.joined_s = 0.0
        self.health_n = self.health_joined = 0
        self.heap_min = None
        self.heap_last = None
        self.minheap_last = None
        self.stack_min: dict[str, int] = {}
        self.beacon_rx = 0
        self.mon_tx = 0
        self.mon_tx_to = Counter()
        self.delivered_from = Counter()
        self.ll_rssi: list[float] = []
        self.ll_snr: list[float] = []
        self.ll_beacons = self.ll_rtables = self.ll_rx = 0
        self.warn = Counter()
        self.pkt_tx = 0


def _add_state_time(days: dict, state: int | None, t0: float, t1: float) -> None:
    """Credit [t0, t1) in `state` to each gateway-local day it spans."""
    if state is None or t1 <= t0:
        return
    t = t0
    while t < t1:
        end = min(t1, (math.floor(t / DAY_S) + 1) * DAY_S)
        d = days[_day(t)]
        d.state_s += end - t
        if state in JOINED_STATES:
            d.joined_s += end - t
        t = end


def parse_node(args) -> dict:
    label, paths, ctx, gap_s = args
    me = v2_runtime_addr(label)
    manager = ctx["manager"]
    ll_peer = ctx["longlink"].get(me)
    days: dict[str, _Day] = defaultdict(_Day)
    beacon_rx_t: list[float] = []
    ll_beacon_t: list[float] = []
    sends: list[tuple[float, str, int]] = []   # (t, dest, seq)
    received: list[tuple[str, int]] = []       # (src, seq) that reached this node
    manager_tx_t: list[float] = []
    heap_fit = [0, 0.0, 0.0, 0.0, 0.0]  # n, sum t, sum h, sum t^2, sum t*h (t in days)
    t_ref = None
    heap_fit_last = 0.0
    cfg = None
    boots: list[list] = []  # [t, label, crash, resolved_by_BOOT_line]
    state: int | None = None
    state_since = 0.0
    prev_t = None
    file_end_t = None

    def transition(new: int, t: float, d: _Day) -> None:
        """Move the tracked state to `new` at t, counting joins/recoveries/elections.
        2.0.0 does not log every change (e.g. JOINING -> NORMAL_OPERATION), so the
        HEALTH line and the library's "(my state: N" tags also drive this."""
        nonlocal state, state_since
        if new == state:
            return
        _add_state_time(days, state, state_since, t)
        if state is not None:  # unknown -> X (after a gap) is not an event
            if new in JOINED_STATES and state not in JOINED_STATES:
                d.joins += 1
            if new == STATE_FAULT_RECOVERY:
                d.fault_recoveries += 1
            if new == STATE_NM_ELECTION:
                d.nm_elections += 1
        state, state_since = new, t

    for path in paths:
        with _open_log(path) as fh:
            for line in fh:
                if len(line) < 26 or line[0] != "[" or line[24] != "]":
                    continue
                ts = line[1:24]
                t = _epoch(ts)
                # A snapshot taken before a rotation overlaps the day file that follows.
                if file_end_t is not None and t <= file_end_t:
                    continue
                d = days[ts[:10]]
                d.lines += 1
                if d.first is None:
                    d.first = t
                d.last = t
                if prev_t is not None and t - prev_t > gap_s:
                    days[_day(prev_t)].gaps += 1
                    days[_day(prev_t)].gap_s += t - prev_t
                    _add_state_time(days, state, state_since, prev_t)
                    state = None  # unknown across a monitor gap
                prev_t = t
                msg = line[26:]

                if "PKT_" in msg:
                    if "PKT_RX" in msg:
                        m = RE_PKT_RX.search(msg)
                        if m:
                            src = m.group(1).upper()[-4:].zfill(4)
                            typ = int(m.group(3), 16)
                            if typ == TYPE_SYNC_BEACON:
                                d.beacon_rx += 1
                                beacon_rx_t.append(t)
                            if ll_peer and src == ll_peer:
                                d.ll_rx += 1
                                d.ll_rssi.append(float(m.group(4)))
                                d.ll_snr.append(float(m.group(5)))
                                if typ == TYPE_SYNC_BEACON:
                                    d.ll_beacons += 1
                                    ll_beacon_t.append(t)
                                elif typ == TYPE_ROUTE_TABLE:
                                    d.ll_rtables += 1
                    elif "PKT_TX" in msg:
                        d.pkt_tx += 1
                        if me == manager:
                            m = RE_PKT_TX.search(msg)
                            if m and int(m.group(3), 16) == TYPE_SYNC_BEACON \
                                    and m.group(2).upper()[-4:].zfill(4) == manager:
                                manager_tx_t.append(t)
                    continue

                if "Network service state changed" in msg:
                    m = RE_STATE.search(msg)
                    if m:
                        transition(int(m.group(1)), t, d)
                    continue
                if "HEALTH node=" in msg:
                    m = RE_HEALTH.search(msg)
                    if m:
                        heap, minheap, st = int(m.group(1)), int(m.group(2)), int(m.group(4))
                        d.health_n += 1
                        d.health_joined += st in JOINED_STATES
                        d.heap_min = heap if d.heap_min is None else min(d.heap_min, heap)
                        d.heap_last, d.minheap_last = heap, minheap
                        if t_ref is None:
                            t_ref = t
                        heap_fit_last = t
                        x = (t - t_ref) / DAY_S
                        heap_fit[0] += 1
                        heap_fit[1] += x
                        heap_fit[2] += heap
                        heap_fit[3] += x * x
                        heap_fit[4] += x * heap
                        transition(st, t, d)
                    continue
                if "STACK[" in msg:
                    m = RE_STACK.search(msg)
                    if m:
                        name, free = m.group(1), int(m.group(3))
                        if free < d.stack_min.get(name, 1 << 30):
                            d.stack_min[name] = free
                    continue
                if "Sending DATA to" in msg:
                    m = RE_SEND.search(msg)
                    if m:
                        dest = m.group(1).upper()[-4:].zfill(4)
                        d.mon_tx += 1
                        d.mon_tx_to[dest] += 1
                        sends.append((t, dest, int(m.group(2))))
                    continue
                if "DATA reached final destination" in msg:
                    m = RE_DELIV.search(msg)
                    if m:
                        src = m.group(1).upper()[-4:].zfill(4)
                        d.delivered_from[src] += 1
                        received.append((src, int(m.group(3))))
                    continue
                if "rst:0x" in msg:
                    m = RE_RST.search(msg)
                    if m:
                        code = m.group(1).lower()
                        boots.append([t, "rst:0x" + code, code in CRASH_RST, False])
                        _add_state_time(days, state, state_since, t)
                        state, state_since = 0, t
                    continue
                if "BOOT node=" in msg:
                    m = RE_BOOT.search(msg)
                    if m:
                        reason = int(m.group(1))
                        label_r = RESET_REASON.get(reason, str(reason))
                        if boots and not boots[-1][3] and t - boots[-1][0] <= BOOT_LINE_WINDOW_S:
                            boots[-1][1:] = [label_r, reason in CRASH_REASONS, True]
                        else:  # the ROM line was lost (e.g. before the monitor attached)
                            boots.append([t, label_r, reason in CRASH_REASONS, True])
                            _add_state_time(days, state, state_since, t)
                            state, state_since = 0, t
                    continue
                if "state: " in msg:
                    m = RE_STATE_SEEN.search(msg)
                    if m:
                        transition(int(m.group(1)), t, d)
                    # no continue: the line may carry other markers
                if "Joining network with manager" in msg:
                    d.join_attempts += 1
                    continue
                if "Join timeout" in msg:
                    d.join_timeouts += 1
                    continue
                if "[gw-monitor] monitor exited" in msg:
                    d.mon_restarts += 1
                    continue
                if cfg is None and "CFG node=" in msg:
                    m = RE_CFG.search(_strip_ansi(msg))
                    if m:
                        cfg = m.group(1)
                    continue
                if ("[WARNING]" in msg or "[ERROR]" in msg or msg[:2] in ("W ", "E ")
                        or msg.startswith(("\x1b[0;33mW", "\x1b[0;31mE"))):
                    clean = _strip_ansi(msg).strip()
                    m = RE_LIBLVL.search(clean)
                    if m:
                        d.warn[f"{m.group(1)[0]} {RE_NORM.sub('N', m.group(2))[:120]}"] += 1
                    else:
                        m = RE_ESPLVL.match(clean)
                        if m:
                            d.warn[f"{m.group(1)} {m.group(2)}: {RE_NORM.sub('N', m.group(3))[:110]}"] += 1
                if any(k in msg for k in CRASH_MARKERS):
                    d.crash_markers += 1
        file_end_t = prev_t
    node_first = min((dd.first for dd in days.values() if dd.first is not None), default=None)
    for t, label_r, crash, _ in boots:
        d = days[_day(t)]
        if node_first is not None and t - node_first <= UPLOAD_BOOT_GRACE_S:
            d.boot_reasons["upload:" + label_r] += 1
            continue
        d.boots += 1
        d.crash_boots += crash
        d.boot_reasons[label_r] += 1
    if prev_t is not None:
        _add_state_time(days, state, state_since, prev_t)

    n, sx, sy, sxx, sxy = heap_fit
    span = (heap_fit_last - t_ref) / DAY_S if t_ref is not None else 0.0
    slope = ((n * sxy - sx * sy) / (n * sxx - sx * sx)
             if n > 2 and span >= MIN_HEAP_FIT_DAYS and (n * sxx - sx * sx) > 1e-9 else None)

    out_days = {}
    for day, d in sorted(days.items()):
        out_days[day] = {
            "lines": d.lines, "first": d.first, "last": d.last,
            "gaps": d.gaps, "gap_s": round(d.gap_s), "mon_restarts": d.mon_restarts,
            "boots": d.boots, "crash_boots": d.crash_boots, "boot_reasons": dict(d.boot_reasons),
            "crash_markers": d.crash_markers,
            "joins": d.joins, "join_attempts": d.join_attempts, "join_timeouts": d.join_timeouts,
            "fault_recoveries": d.fault_recoveries, "nm_elections": d.nm_elections,
            "state_s": round(d.state_s), "joined_s": round(d.joined_s),
            "health_n": d.health_n, "health_joined": d.health_joined,
            "heap_min": d.heap_min, "heap_last": d.heap_last, "minheap_last": d.minheap_last,
            "stack_min": d.stack_min, "beacon_rx": d.beacon_rx, "pkt_tx": d.pkt_tx,
            "mon_tx": d.mon_tx, "mon_tx_to": dict(d.mon_tx_to),
            "delivered_from": dict(d.delivered_from),
            "ll_rx": d.ll_rx, "ll_beacons": d.ll_beacons, "ll_rtables": d.ll_rtables,
            "ll_rssi_med": _q(d.ll_rssi, 0.5), "ll_rssi_p10": _q(d.ll_rssi, 0.1),
            "ll_snr_med": _q(d.ll_snr, 0.5), "ll_snr_p10": _q(d.ll_snr, 0.1),
            "warn": dict(d.warn),
        }
    return {"label": label, "addr": me, "files": [p.name for p in paths], "cfg": cfg,
            "heap_slope_b_per_day": slope,
            "days": out_days, "_beacon_rx_t": beacon_rx_t, "_ll_beacon_t": ll_beacon_t,
            "_sends": sends, "_received": received, "_manager_tx_t": manager_tx_t}


def _q(xs: list[float], q: float):
    if not xs:
        return None
    s = sorted(xs)
    return round(s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))], 1)


def beacon_coverage(nodes: dict, manager_label: str) -> None:
    """Per node/day: superframes (manager beacon TX) with >= 1 sync beacon received."""
    mgr = nodes.get(manager_label)
    tx = sorted(mgr["_manager_tx_t"]) if mgr else []
    if len(tx) < 2:
        return
    period = statistics.median(b - a for a, b in zip(tx, tx[1:]))
    expected = Counter(_day(t) for t in tx)  # the manager's own days
    def superframes_hit(times: list[float]) -> set[int]:
        hit: set[int] = set()
        for t in times:
            i = bisect.bisect_right(tx, t + BEACON_SKEW_S) - 1
            if i >= 0 and t - tx[i] < period:
                hit.add(i)
        return hit

    for label, nd in nodes.items():
        if label == manager_label:
            continue
        hit = superframes_hit(nd["_beacon_rx_t"])
        ll_hit = superframes_hit(nd["_ll_beacon_t"])
        for day, dd in nd["days"].items():
            # only superframes while this node was logging (it may be uploaded late)
            lo = bisect.bisect_left(tx, dd["first"] - BEACON_SKEW_S) if dd["first"] else 0
            hi = bisect.bisect_right(tx, dd["last"]) if dd["last"] else 0
            span = range(lo, hi)
            exp = len(span)
            covered = Counter(_day(tx[i]) for i in hit if lo <= i < hi) if exp else Counter()
            ll_covered = (Counter(_day(tx[i]) for i in ll_hit if lo <= i < hi) if exp else Counter())
            dd["superframes"] = exp
            dd["beacon_cov"] = round(covered.get(day, 0) / exp, 4) if exp else None
            # superframes in which the long-link peer's own beacon was heard directly
            dd["ll_beacon_cov"] = round(ll_covered.get(day, 0) / exp, 4) if exp else None
    for day, dd in mgr["days"].items():
        dd["superframes"] = expected.get(day, 0)
        dd["beacon_cov"] = dd["ll_beacon_cov"] = None
    nodes[manager_label]["superframe_s"] = round(period, 2)


def _sum(nd: dict, key: str) -> float:
    return sum(dd.get(key) or 0 for dd in nd["days"].values())


def summarize(nodes: dict, ctx: dict, gap_s: float) -> dict:
    labels = list(nodes)
    all_first = min((dd["first"] for nd in nodes.values() for dd in nd["days"].values() if dd["first"]),
                    default=None)
    all_last = max((dd["last"] for nd in nodes.values() for dd in nd["days"].values() if dd["last"]),
                   default=None)
    # dest -> {(src, seq)} that reached it; every logged node that received DATA is an uplink
    arrived: dict[str, set] = {nd["addr"]: set(nd["_received"]) for nd in nodes.values()}
    logged = set(arrived)
    uplinks = sorted(a for a, got in arrived.items() if got)
    cutoff = (all_last - IN_FLIGHT_S) if all_last else None

    rows = {}
    for label, nd in nodes.items():
        days = nd["days"]
        first = min((dd["first"] for dd in days.values() if dd["first"]), default=None)
        last = max((dd["last"] for dd in days.values() if dd["last"]), default=None)
        state_s, joined_s = _sum(nd, "state_s"), _sum(nd, "joined_s")
        boots = int(_sum(nd, "boots"))
        stack_min: dict[str, int] = {}
        for dd in days.values():
            for k, v in dd["stack_min"].items():
                stack_min[k] = min(v, stack_min.get(k, v))
        sf = sum(dd.get("superframes") or 0 for dd in days.values() if dd.get("beacon_cov") is not None)
        cov = sum((dd.get("beacon_cov") or 0) * (dd.get("superframes") or 0) for dd in days.values()
                  if dd.get("beacon_cov") is not None)
        heap_days = [dd for dd in days.values() if dd["heap_min"] is not None]
        sent_to: Counter = Counter()
        for dd in days.values():
            sent_to.update(dd["mon_tx_to"])
        # Only destinations whose log we have can confirm delivery, and sends in the last
        # IN_FLIGHT_S may still be on their way; a send counts once its seq shows up there.
        judged = [(dest, seq) for t, dest, seq in nd["_sends"]
                  if dest in logged and cutoff is not None and t <= cutoff]
        mon_tx_gw = len(judged)
        mon_deliv = sum((nd["addr"], seq) in arrived[dest] for dest, seq in judged)
        rows[label] = {
            "cluster": "remote" if label in ctx["remote"] else "main",
            "addr": nd["addr"],
            "first": first, "last": last,
            "silent_at_end_s": round(all_last - last) if all_last and last else None,
            "gaps": int(_sum(nd, "gaps")), "gap_s": int(_sum(nd, "gap_s")),
            "mon_restarts": int(_sum(nd, "mon_restarts")),
            # boots exclude the upload's (within UPLOAD_BOOT_GRACE_S of the node's first line)
            "boots": boots,
            "reboots": boots,
            "crash_boots": int(_sum(nd, "crash_boots")),
            "crash_markers": int(_sum(nd, "crash_markers")),
            "joins": int(_sum(nd, "joins")),
            "rejoins": max(0, int(_sum(nd, "joins")) - 1),
            "join_attempts": int(_sum(nd, "join_attempts")),
            "join_timeouts": int(_sum(nd, "join_timeouts")),
            "fault_recoveries": int(_sum(nd, "fault_recoveries")),
            "nm_elections": int(_sum(nd, "nm_elections")),
            "joined_pct": round(100 * joined_s / state_s, 2) if state_s else None,
            "health_joined_pct": (round(100 * _sum(nd, "health_joined") / _sum(nd, "health_n"), 2)
                                  if _sum(nd, "health_n") else None),
            "beacon_cov_pct": round(100 * cov / sf, 2) if sf else None,
            "heap_first_day_min": heap_days[0]["heap_min"] if heap_days else None,
            "heap_last_day_min": heap_days[-1]["heap_min"] if heap_days else None,
            "minheap_last": heap_days[-1]["minheap_last"] if heap_days else None,
            "heap_slope_b_per_day": (round(nd["heap_slope_b_per_day"])
                                     if nd["heap_slope_b_per_day"] is not None else None),
            "stack_min": stack_min,
            "stack_min_free": min(stack_min.values()) if stack_min else None,
            "mon_tx": int(_sum(nd, "mon_tx")),
            "mon_tx_to": dict(sent_to),
            "mon_tx_gw": mon_tx_gw,
            "mon_delivered": mon_deliv,
            "mon_delivery_pct": round(100 * mon_deliv / mon_tx_gw, 1) if mon_tx_gw else None,
            "warn_lines": int(sum(sum(dd["warn"].values()) for dd in days.values())),
            "cfg": nd["cfg"],
        }

    # Long link, per day, from both ends
    longlink = {}
    for label, nd in nodes.items():
        peer = ctx["longlink"].get(nd["addr"])
        if not peer:
            continue
        longlink[f"{peer}->{nd['addr']}"] = {
            day: {k: dd[k] for k in ("ll_rx", "ll_beacons", "ll_rtables", "ll_rssi_med", "ll_rssi_p10",
                                     "ll_snr_med", "ll_snr_p10")} | {"superframes": dd.get("superframes"),
                                                       "ll_beacon_cov": dd.get("ll_beacon_cov")}
            for day, dd in nd["days"].items()}

    # Warning templates across the fleet: total and the first day each appeared
    warn_total: Counter = Counter()
    warn_first: dict[str, str] = {}
    warn_nodes: dict[str, set] = defaultdict(set)
    for label, nd in nodes.items():
        for day, dd in sorted(nd["days"].items()):
            for k, v in dd["warn"].items():
                warn_total[k] += v
                warn_nodes[k].add(label)
                if k not in warn_first or day < warn_first[k]:
                    warn_first[k] = day
    warnings = [{"template": k, "count": v, "first_day": warn_first[k], "nodes": sorted(warn_nodes[k])}
                for k, v in warn_total.most_common(40)]

    checks = fleet_checks(rows)
    mgr = nodes.get(ctx["manager_label"], {})
    return {
        "summary_version": SUMMARY_VERSION,
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "data_from": _iso(all_first), "data_up_to": _iso(all_last),
        "days": sorted({d for nd in nodes.values() for d in nd["days"]}),
        "gap_threshold_s": gap_s,
        "manager": ctx["manager"], "uplinks": uplinks, "in_flight_s": IN_FLIGHT_S,
        "superframe_s": mgr.get("superframe_s"),
        "checks": checks,
        "nodes": rows,
        "per_day": {label: {day: _day_row(dd) for day, dd in nd["days"].items()}
                    for label, nd in nodes.items()},
        "longlink": longlink,
        "warnings": warnings,
    }


def _day_row(dd: dict) -> dict:
    keep = ("lines", "gaps", "gap_s", "mon_restarts", "boots", "crash_boots", "boot_reasons",
            "crash_markers", "joins", "join_timeouts", "fault_recoveries", "nm_elections",
            "heap_min", "minheap_last", "beacon_cov", "superframes", "mon_tx",
            "stack_min")
    row = {k: dd.get(k) for k in keep}
    row["joined_pct"] = round(100 * dd["joined_s"] / dd["state_s"], 2) if dd["state_s"] else None
    row["warn_lines"] = sum(dd["warn"].values())
    return row


def _iso(t):
    return datetime.fromtimestamp(t, tz=timezone.utc).strftime("%Y-%m-%d %H:%M") if t else None


def _rag(value, green, amber, higher_is_better=True):
    if value is None:
        return "n/a"
    if higher_is_better:
        return "green" if value >= green else "amber" if value >= amber else "red"
    return "green" if value <= green else "amber" if value <= amber else "red"


def fleet_checks(rows: dict) -> list[dict]:
    main = {k: r for k, r in rows.items() if r["cluster"] == "main"}
    remote = {k: r for k, r in rows.items() if r["cluster"] == "remote"}

    def worst(rs, key, fn):
        vals = [(r[key], k) for k, r in rs.items() if r[key] is not None]
        return fn(vals) if vals else (None, None)

    checks = []
    reboots = sum(r["reboots"] for r in main.values())
    crash = sum(r["crash_boots"] + r["crash_markers"] for r in main.values())
    checks.append({"check": "Main cluster: unexpected reboots", "value": reboots,
                   "status": "green" if reboots == 0 else "red",
                   "detail": ", ".join(f"{k}×{r['reboots']}" for k, r in main.items() if r["reboots"]) or "none"})
    checks.append({"check": "Main cluster: crash resets / panics", "value": crash,
                   "status": "green" if crash == 0 else "red",
                   "detail": ", ".join(f"{k}×{r['crash_boots'] + r['crash_markers']}" for k, r in main.items()
                                       if r["crash_boots"] + r["crash_markers"]) or "none"})
    fr = sum(r["fault_recoveries"] for r in main.values())
    checks.append({"check": "Main cluster: fault recoveries", "value": fr,
                   "status": "green" if fr == 0 else "red",
                   "detail": ", ".join(f"{k}×{r['fault_recoveries']}" for k, r in main.items()
                                       if r["fault_recoveries"]) or "none"})
    v, k = worst(main, "joined_pct", min)
    checks.append({"check": "Main cluster: time joined (worst node)", "value": v,
                   "status": _rag(v, 99.0, 95.0), "detail": k or ""})
    v, k = worst(main, "beacon_cov_pct", min)
    checks.append({"check": "Main cluster: sync-beacon coverage (worst node)", "value": v,
                   "status": _rag(v, 95.0, 80.0), "detail": k or ""})
    v, k = worst(rows, "heap_slope_b_per_day", min)
    checks.append({"check": "Heap trend, bytes/day (worst node)", "value": v,
                   "status": "n/a" if v is None else _rag(v, -300, -1000), "detail": k or ""})
    v, k = worst(rows, "stack_min_free", min)
    checks.append({"check": "Task stack minimum free, bytes (worst)", "value": v,
                   "status": _rag(v, 512, 256), "detail": k or ""})
    gaps = sum(r["gaps"] for r in rows.values())
    restarts = sum(r["mon_restarts"] for r in rows.values())
    checks.append({"check": "Logging: monitor gaps / restarts", "value": f"{gaps} / {restarts}",
                   "status": "green" if gaps == 0 else "amber",
                   "detail": ", ".join(f"{k}:{r['gaps']}" for k, r in rows.items() if r["gaps"]) or "none"})
    deliv = [r["mon_delivery_pct"] for r in main.values() if r["mon_delivery_pct"] is not None]
    v = round(min(deliv), 1) if deliv else None
    checks.append({"check": "Monitor messages delivered to uplink (worst main node)", "value": v,
                   "status": _rag(v, 95.0, 80.0), "detail": ""})
    # Remote cluster and long link: reported, not gated
    v, k = worst(remote, "joined_pct", min)
    checks.append({"check": "Remote cluster: time joined (worst, not gated)", "value": v,
                   "status": "info", "detail": k or ""})
    rj = sum(r["rejoins"] for r in remote.values())
    checks.append({"check": "Remote cluster: rejoins (not gated)", "value": rj, "status": "info",
                   "detail": ", ".join(f"{k}×{r['rejoins']}" for k, r in remote.items() if r["rejoins"]) or "none"})
    return checks


def build(log_dir: Path, ctx: dict, gap_s: float, jobs: int) -> dict:
    files = node_files(log_dir)
    if not files:
        raise SystemExit(f"no monitor-dev-*.log* files in {log_dir}")
    with Pool(min(jobs, len(files))) as pool:
        results = pool.map(parse_node, [(l, p, ctx, gap_s) for l, p in files.items()], chunksize=1)
    nodes = {r["label"]: r for r in results}
    beacon_coverage(nodes, ctx["manager_label"])
    out = summarize(nodes, ctx, gap_s)
    out["log_dir"] = str(log_dir)
    return out


def print_text(rep: dict) -> None:
    print(f"Soak report  {rep['data_from']} -> {rep['data_up_to']}  ({len(rep['days'])} day(s), "
          f"superframe {rep['superframe_s']} s, uplinks {', '.join(rep['uplinks']) or 'none logged'})")
    print()
    for c in rep["checks"]:
        print(f"  [{c['status']:>5}] {c['check']}: {c['value']}  {c['detail']}")
    print()
    hdr = (f"{'node':5} {'cl':6} {'boot':>4} {'rbt':>3} {'crash':>5} {'gaps':>4} {'join':>4} {'FR':>3} "
           f"{'joined%':>7} {'bcn%':>6} {'heap0':>6} {'heapN':>6} {'slope':>6} {'stk':>5} "
           f"{'monTX':>5} {'deliv%':>6} {'warn':>6}")
    print(hdr)
    for k, r in rep["nodes"].items():
        def f(v, w, p=None):
            return f"{'-' if v is None else (f'{v:.{p}f}' if p is not None else v):>{w}}"
        print(f"{k:5} {r['cluster']:6} {f(r['boots'], 4)} {f(r['reboots'], 3)} "
              f"{f(r['crash_boots'] + r['crash_markers'], 5)} {f(r['gaps'], 4)} {f(r['joins'], 4)} "
              f"{f(r['fault_recoveries'], 3)} {f(r['joined_pct'], 7, 2)} {f(r['beacon_cov_pct'], 6, 1)} "
              f"{f(r['heap_first_day_min'], 6)} {f(r['heap_last_day_min'], 6)} "
              f"{f(r['heap_slope_b_per_day'], 6)} {f(r['stack_min_free'], 5)} {f(r['mon_tx'], 5)} "
              f"{f(r['mon_delivery_pct'], 6, 1)} {f(r['warn_lines'], 6)}")
    if rep["longlink"]:
        print()
        for edge, days in rep["longlink"].items():
            print(f"Long link {edge}:")
            for day, v in days.items():
                cov = "-" if v["ll_beacon_cov"] is None else f"{100 * v['ll_beacon_cov']:.1f}%"
                print(f"  {day}  rx={v['ll_rx']:5} beacon cov={cov:>6} of {v['superframes'] or '-':<4} "
                      f"rtables={v['ll_rtables']:4}  RSSI med/p10 {v['ll_rssi_med']}/{v['ll_rssi_p10']}  "
                      f"SNR med/p10 {v['ll_snr_med']}/{v['ll_snr_p10']}")
    if rep["warnings"]:
        print()
        print("Top WARNING/ERROR templates:")
        for w in rep["warnings"][:15]:
            print(f"  {w['count']:7}  first {w['first_day']}  {len(w['nodes']):2} node(s)  {w['template'][:100]}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log_dir", type=Path)
    ap.add_argument("--json", type=Path, help="write the full report as JSON")
    ap.add_argument("--manager", default="006C")
    ap.add_argument("--remote", default="3428,DD58,14A4", help="remote-cluster node labels")
    ap.add_argument("--longlink", default="006C:3428", help="A:B labels of the long link")
    ap.add_argument("--gap-s", type=float, default=600.0, help="log silence counted as a monitor gap")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()

    la, lb = (v2_runtime_addr(x) for x in a.longlink.split(":"))
    ctx = {"manager": v2_runtime_addr(a.manager), "manager_label": a.manager.upper(),
           "remote": {x.strip().upper() for x in a.remote.split(",") if x.strip()},
           "longlink": {la: lb, lb: la}}
    rep = build(a.log_dir, ctx, a.gap_s, a.jobs)
    if a.json:
        a.json.parent.mkdir(parents=True, exist_ok=True)
        a.json.write_text(json.dumps(rep, indent=1))
    if not a.quiet:
        print_text(rep)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
