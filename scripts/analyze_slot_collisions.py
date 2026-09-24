#!/usr/bin/env python3
"""
analyze_slot_collisions.py

Analyzes LoRaChat v2 testbed logs to detect:
1. TX data slot collisions (multiple nodes transmitting in the same slot simultaneously)
2. Missed slot transitions (a node skipped a slot, e.g. due to SYNC_BEACON_TX blocking delay)

Usage:
    python3 scripts/analyze_slot_collisions.py [log_dir]
    Default log_dir: logs_testbed/cap1-v2-logs
"""

import os
import re
import sys
from collections import defaultdict
from datetime import datetime, timedelta

# ──────────────────────────────────────────────────────────────────────────────
# Parsing helpers
# ──────────────────────────────────────────────────────────────────────────────

# Strip both: real ESC sequences (\x1b[Nm) and the UTF-8 "SYMBOL FOR ESCAPE"
# (U+241B = \xe2\x90\x9b) used by PlatformIO's colorize filter.
_ANSI_BYTES = re.compile(rb'(?:\xe2\x90\x9b|\x1b)\[[0-9;]*m')

# Slot transition line after ANSI stripping:
# "2026-03-13 07-44-47.694743 [INFO] [0xDD58] Slot 3 transition: type=SYNC_BEACON_TX"
_TRANSITION_RE = re.compile(
    r"(\d{4}-\d{2}-\d{2}) (\d{2})-(\d{2})-(\d{2})\.(\d+) "
    r"\[\w+\] \[0x([0-9A-Fa-f]+)\] "
    r"Slot (\d+) transition: type=(\S+)"
)

SLOT_DURATION_MS = 1000  # nominal slot duration


def _parse_ts(date: str, hh: str, mm: str, ss: str, frac: str) -> datetime:
    frac6 = (frac + "000000")[:6]
    return datetime(
        int(date[:4]), int(date[5:7]), int(date[8:10]),
        int(hh), int(mm), int(ss), int(frac6),
    )


def load_log(filepath: str) -> list[dict]:
    events = []
    with open(filepath, "rb") as f:
        for raw in f:
            line = _ANSI_BYTES.sub(b"", raw).decode("utf-8", errors="replace").rstrip()
            m = _TRANSITION_RE.search(line)
            if m:
                date, hh, mm, ss, frac, node_hex, slot_str, slot_type = m.groups()
                events.append({
                    "ts": _parse_ts(date, hh, mm, ss, frac),
                    "ts_str": f"{date} {hh}:{mm}:{ss}.{frac[:6]}",
                    "node": node_hex.upper(),
                    "slot": int(slot_str),
                    "type": slot_type,
                })
    return events


# ──────────────────────────────────────────────────────────────────────────────
# Analysis 1: TX Data Slot Collisions
# ──────────────────────────────────────────────────────────────────────────────

# Two TX events are "same superframe" if they share the same slot index AND
# their timestamps are within COLLISION_WINDOW_S of each other.
COLLISION_WINDOW_S = 2.0


def find_tx_collisions(all_events: list[dict]) -> list[dict]:
    tx = [e for e in all_events if e["type"] == "TX"]
    tx.sort(key=lambda e: e["ts"])

    collisions = []
    seen_keys: set = set()

    for i, a in enumerate(tx):
        partners = []
        for b in tx[i + 1:]:
            delta = (b["ts"] - a["ts"]).total_seconds()
            if delta > COLLISION_WINDOW_S:
                break
            if b["slot"] == a["slot"] and b["node"] != a["node"]:
                partners.append(b)

        if partners:
            group = [a] + partners
            nodes_key = frozenset(e["node"] for e in group)
            dedup_key = (a["slot"], nodes_key)
            if dedup_key not in seen_keys:
                seen_keys.add(dedup_key)
                collisions.append({
                    "slot": a["slot"],
                    "events": sorted(group, key=lambda e: e["ts"]),
                })

    return collisions


# ──────────────────────────────────────────────────────────────────────────────
# Analysis 2: Missed Slot Transitions
# ──────────────────────────────────────────────────────────────────────────────

# A "genuine" missed transition is when a node jumps from slot N to slot N+k
# (k > 1) and the elapsed wall-clock time is close to what k slots would take.
# If the gap is much larger, the node was simply off or logging was paused.
SLOT_DURATION_TOLERANCE = 3.0   # actual_gap must be ≤ this × expected_gap


def find_missed_transitions(all_events: list[dict]) -> list[dict]:
    by_node: dict[str, list] = defaultdict(list)
    for e in all_events:
        by_node[e["node"]].append(e)

    missed = []
    for node, events in sorted(by_node.items()):
        events.sort(key=lambda e: e["ts"])
        for idx in range(1, len(events)):
            prev, curr = events[idx - 1], events[idx]
            jump = curr["slot"] - prev["slot"]
            if jump <= 1:          # sequential or wrap-around → skip
                continue
            gap_ms = (curr["ts"] - prev["ts"]).total_seconds() * 1000
            expected_ms = jump * SLOT_DURATION_MS
            # Filter out log gaps: only flag when gap ≈ expected (within 3×)
            if gap_ms > expected_ms * SLOT_DURATION_TOLERANCE:
                continue
            missed.append({
                "node": node,
                "from_slot": prev["slot"],
                "from_type": prev["type"],
                "from_ts": prev["ts_str"],
                "from_ts_dt": prev["ts"],
                "to_slot": curr["slot"],
                "to_type": curr["type"],
                "to_ts": curr["ts_str"],
                "skipped": list(range(prev["slot"] + 1, curr["slot"])),
                "gap_ms": gap_ms,
                "expected_ms": expected_ms,
            })
    return missed


# ──────────────────────────────────────────────────────────────────────────────
# Deep-dive: DD58 around 07:44:47
# ──────────────────────────────────────────────────────────────────────────────

def dd58_deep_dive(all_events: list[dict]) -> list[dict]:
    t_start = datetime(2026, 3, 13, 7, 44, 40)
    t_end   = datetime(2026, 3, 13, 7, 45,  5)
    return sorted(
        [e for e in all_events if e["node"] == "DD58" and t_start <= e["ts"] <= t_end],
        key=lambda e: e["ts"],
    )


# ──────────────────────────────────────────────────────────────────────────────
# Report helpers
# ──────────────────────────────────────────────────────────────────────────────

def section(title: str):
    bar = "=" * 72
    print(f"\n{bar}\n  {title}\n{bar}")


def main():
    log_dir = sys.argv[1] if len(sys.argv) > 1 else "logs_testbed/cap1-v2-logs"
    log_files = sorted(f for f in os.listdir(log_dir) if f.endswith(".log"))
    if not log_files:
        print(f"No .log files found in {log_dir}")
        sys.exit(1)

    print(f"Loading {len(log_files)} log files from '{log_dir}' ...")
    all_events: list[dict] = []
    for fname in log_files:
        events = load_log(os.path.join(log_dir, fname))
        all_events.extend(events)
        print(f"  {fname}: {len(events):6,} slot transitions")

    print(f"\nTotal slot transition events loaded: {len(all_events):,}")

    # ── 1. TX Slot Collisions ─────────────────────────────────────────────────
    section("TX DATA SLOT COLLISIONS")
    print(
        f"Two or more nodes fire a TX transition at the SAME slot index within\n"
        f"a {COLLISION_WINDOW_S:.0f}-second window → they are transmitting simultaneously.\n"
    )
    collisions = find_tx_collisions(all_events)
    # Remove subsets: drop any group whose node-set is a strict subset of another group
    # at the same slot and overlapping time window.
    def is_subset_collision(c: dict, others: list[dict]) -> bool:
        cn = frozenset(e["node"] for e in c["events"])
        ct = c["events"][0]["ts"]
        for o in others:
            if o is c:
                continue
            on = frozenset(e["node"] for e in o["events"])
            ot = o["events"][0]["ts"]
            if cn < on and o["slot"] == c["slot"] and abs((ct - ot).total_seconds()) < COLLISION_WINDOW_S:
                return True
        return False
    collisions = [c for c in collisions if not is_subset_collision(c, collisions)]

    if not collisions:
        print("  No TX data slot collisions detected.")
    else:
        print(f"  {len(collisions)} collision group(s):\n")
        for i, c in enumerate(collisions, 1):
            nodes = sorted({e["node"] for e in c["events"]})
            t0 = c["events"][0]["ts"]
            t1 = c["events"][-1]["ts"]
            spread_ms = (t1 - t0).total_seconds() * 1000
            print(f"  [{i:3d}]  slot={c['slot']:3d}  nodes={', '.join('0x'+n for n in nodes)}"
                  f"  spread={spread_ms:.1f} ms")
            for e in c["events"]:
                print(f"          {e['ts_str']}  0x{e['node']}  slot={e['slot']}")
            print()

    # ── 1b. Root cause explanation ────────────────────────────────────────────
    section("ROOT CAUSE — TX SLOT COLLISIONS")
    print("""\
  The TDMA slot table is built INDEPENDENTLY by each node from its own local
  routing-table snapshot (network_service.cpp, UpdateSlotTable, Phase 3 ~line 1799):

      slot_index = 0   ← shared counter advancing through all phases
      for each node N in ordered_nodes:
          for j in range(N.allocated_data_slots):
              if N == self:       AllocateSlot(TX,    N, slot_index++)
              elif N.is_neighbor: AllocateSlot(RX,    N, slot_index++)
              else:               AllocateSlot(SLEEP, N, slot_index++)

  If two nodes have DIVERGED routing tables at the moment they compute the slot
  table (e.g. one knows about a node the other doesn't, or has a different
  allocated_data_slots value for a peer), their slot_index counter diverges.
  Both may end up assigning their own TX slot to the SAME superframe position.

  Trigger: routing-table divergence during network churn (node join / leave /
  topology change). No collision detection or global coordinator exists.
  The protocol assumes all nodes converge to an identical routing table before
  any node applies a slot-table update.\
""")

    # ── 2. Missed Transitions ─────────────────────────────────────────────────
    section("MISSED SLOT TRANSITIONS")
    print(
        "A node 'misses' slot N when its logged sequence jumps from slot N-1\n"
        "to slot N+1 (or higher) within approximately the expected elapsed time.\n"
        "Log gaps (node off / logging paused) are filtered out.\n"
    )
    missed = find_missed_transitions(all_events)

    if not missed:
        print("  No missed transitions detected.")
    else:
        # Group by preceding slot type
        by_cause: dict[str, list] = defaultdict(list)
        for m in missed:
            by_cause[m["from_type"]].append(m)

        print(f"  Total genuine missed-slot events: {len(missed)}")
        print(f"  Breakdown by preceding slot type:")
        for t, lst in sorted(by_cause.items(), key=lambda x: -len(x[1])):
            print(f"    {t:30s}: {len(lst):3d}")
        print()

        print("  All missed transitions:\n")
        for m in sorted(missed, key=lambda x: x["from_ts_dt"]):
            skipped_str = (
                str(m["skipped"]) if len(m["skipped"]) <= 6
                else f"[{m['skipped'][0]}..{m['skipped'][-1]}] ({len(m['skipped'])} slots)"
            )
            flag = " ◄ SYNC_BEACON_TX" if m["from_type"] == "SYNC_BEACON_TX" else ""
            print(f"  Node 0x{m['node']}  "
                  f"slot {m['from_slot']}({m['from_type']}) → slot {m['to_slot']}({m['to_type']})"
                  f"{flag}")
            print(f"    Skipped: {skipped_str}")
            print(f"    gap={m['gap_ms']:.0f} ms  (expected≈{m['expected_ms']:.0f} ms)")
            print(f"    From: {m['from_ts']}")
            print(f"    To:   {m['to_ts']}")
            print()

    # ── 3. DD58 Deep-dive ─────────────────────────────────────────────────────
    section("DEEP DIVE — Node 0xDD58 at 07:44:47")
    print(
        "Expected: slot 3 → slot 4 (CONTROL_RX) → slot 5 (CONTROL_RX)\n"
        "Actual:   slot 3 (SYNC_BEACON_TX) → [MISSING slot 4] → slot 5 (CONTROL_RX)\n"
    )
    dive = dd58_deep_dive(all_events)
    if dive:
        print("  DD58 slot transitions [07:44:40 – 07:45:05]:\n")
        prev_slot = None
        for e in dive:
            note = ""
            if prev_slot is not None and e["slot"] - prev_slot > 1 and e["slot"] - prev_slot < 50:
                skipped = list(range(prev_slot + 1, e["slot"]))
                note = f"  ◄◄◄  SKIPPED slot(s): {skipped}"
            marker = "***" if e["slot"] in (3, 4, 5) else "   "
            print(f"  {marker} {e['ts_str']}  slot={e['slot']:3d}  {e['type']}{note}")
            prev_slot = e["slot"]
    else:
        print("  No events found in that window.")

    section("ROOT CAUSE — DD58 MISSED CONTROL_RX AT 07:44:47")
    print("""\
  Timeline reconstruction:

    07:44:47.268  DD58 receives a sync beacon from 0x3428
                  → Triggers re-synchronisation: superframe service STOPPED,
                    slot table rebuilt (155 slots), service RESTARTED.

    07:44:47.678  Superframe service RESTARTED with new timing origin T₀.
                  Slot 3 window = [T₀+3000 ms … T₀+4000 ms]
                                = [07:44:47.678 … 07:44:48.678]

    07:44:47.694  >>> Slot 3 transition: SYNC_BEACON_TX <<<
                  DD58 is at high hop distance; its assigned TX subslot index
                  (= node_address % num_subslots) falls late in the slot.
                  The handler enters a BLOCKING DELAY:
                      GetRTOS().delay(subslot_offset_ms);   // ← key line
                  waiting ~853 ms for its TX window.

    07:44:48.531  PKT_TX: sync beacon broadcast.
    07:44:48.678  "Sent sync beacon in subslot 4" — right at the slot boundary.
                  The blocking delay returns AFTER slot 3 has ended.
                  The superframe timer has already fired the slot 4 callback,
                  but the FreeRTOS protocol task was blocked and missed it.

    07:44:49.672  >>> Slot 5 transition: CONTROL_RX <<<
                  DD58 resumes and processes slot 5, silently skipping slot 4.

  All 14 other nodes were in SLEEP during slot 3 and transitioned normally:
    0x3428  07:44:47.661  SLEEP  →  07:44:48.661  CONTROL_RX  ✓
    0x14A4  07:44:47.695  SYNC_BEACON_RX  →  07:44:48.673  CONTROL_RX  ✓
    0x006C  07:44:47.635  SLEEP  →  07:44:48.631  CONTROL_RX  ✓
    … (all other nodes identical)

  Affected code:
    lib/loramesher/src/protocols/lora_mesh/lora_mesh_protocol.cpp
    SYNC_BEACON_TX handler, ~line 1041:
        GetRTOS().delay(delay_ms);  // can block task past slot boundary

  Fix direction (not in scope of this script):
    Replace blocking delay with a timed check:
        delay_remaining = subslot_offset_ms - GetTimeInSlot();
        if (delay_remaining > 0 && delay_remaining < slot_remaining_ms - guard)
            GetRTOS().delay(delay_remaining);
        // else: subslot window already passed — skip TX this cycle\
""")

    print("\n" + "=" * 72)
    print("  Analysis complete.")
    print("=" * 72 + "\n")


if __name__ == "__main__":
    main()
