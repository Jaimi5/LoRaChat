#!/usr/bin/env python3
"""Per-run log checks for the routing-slice fix (LoRaMesher PR #136).

Reports per node: routing message (0x32) sizes and entry counts, the longest run
of tail slices, TX skips on routing messages and whether the same slice went out
at the next control slot. Per run: [UNIDIRECTIONAL] marks on links where both
ends hear each other's routing (+-120 s), relay routes while the destination is
heard directly (+-60 s), routes of >= 3 hops, the manager's max_hops and
superframe changes, resets / fatal lines / stack headroom, WARN/ERROR counts,
and (with --group) per-member group delivery and missed seqs.

usage: python3 analysis/routing_slice_checks.py RUN_DIR... [--json OUT] [--sender 7B6C] [--group]
"""
import argparse
import gzip
import json
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from analysis.parse_logs import v2_runtime_addr, load_measurement_window  # noqa: E402

ANSI = re.compile(r"\x1b\[[0-9;]*m")
TS = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]")
R_TX32 = re.compile(r"PKT_TX dst=0x\w+ src=0x\w+ type=0x32 size=(\d+)")
R_RX32 = re.compile(r"PKT_RX src=0x(\w+) dst=0x\w+ type=0x32 size=\d+")
R_CREATED = re.compile(r"Created routing table message .*entry count: (\d+)")
R_SKIP = re.compile(r"TX skip: in_slot=(\d+) \+ delay=(\d+) \+ ToA=(\d+) \+ margin=(\d+) = (\d+) > slot=(\d+)")
R_UNI = re.compile(r"Source 0x(\w+): remote_quality=(\d+) .*\[UNIDIRECTIONAL\]")
R_ROUTE = re.compile(r"Route updated: dest=0x(\w+) via=0x(\w+) hops=(\d+)")
R_MAXH = re.compile(r"Updated network max_hops to (\d+)")
R_BEACON = re.compile(r"Queued sync beacon for transmission: (\d+) total slots, (\d+) max hops")
R_STACK = re.compile(r"STACK\[(\w+)\] total=(\d+) free=(\d+)")
R_RST = re.compile(r"rst:0x(\w+) \((\w+)\)")
R_SLOTTAB = re.compile(r"SlotTable\[(\d+)\]")
R_WARN = re.compile(r"\[(WARNING|ERROR)\] \[0x\w+\] (.*)")
R_FATAL = re.compile(r"Guru Meditation|Backtrace:|abort\(\) was called|assert failed|Stack canary")


def ts(line):
    m = TS.match(line)
    return datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp() if m else None


def norm(msg):
    return re.sub(r"\d+", "N", re.sub(r"0x[0-9A-Fa-f]+", "X", msg)).strip()[:110]


def read_node(p):
    out = []
    with gzip.open(p, "rt", errors="replace") if p.suffix == ".gz" else open(p, errors="replace") as f:
        for raw in f:
            line = ANSI.sub("", raw.rstrip("\n"))
            out.append((ts(line), line))
    return out


def active_nodes(run):
    import yaml
    cfg = yaml.safe_load((run / "config.yaml").read_text()) or {}
    devs = cfg.get("devices") or {}
    return {str(k).upper() for k, v in devs.items() if not (isinstance(v, dict) and v.get("node_active", 1) == 0)}


def analyse(run, sender):
    act = active_nodes(run)
    w0, w1 = load_measurement_window(run)
    inwin = lambda t: t is not None and w0 is not None and w0 <= t <= w1  # noqa: E731
    logs = {}
    for p in sorted((run / "logs").glob("monitor-*.log*")):
        nid = p.name.split("-")[-1].split(".")[0].upper()
        if nid in act:
            logs[nid] = read_node(p)
    rt = {n: v2_runtime_addr(n) for n in logs}           # physical -> runtime
    phys = {v: k for k, v in rt.items()}
    # direct 0x32 receptions: heard[node][src_phys] = [t...]
    heard = defaultdict(lambda: defaultdict(list))
    for n, lines in logs.items():
        for t, l in lines:
            m = R_RX32.search(l)
            if m and t:
                heard[n][phys.get(m.group(1).upper(), m.group(1).upper())].append(t)

    def heard_near(a, b, t, w=120):
        return any(abs(x - t) <= w for x in heard[a].get(b, ()))

    res = {"run": run.name, "nodes": {}, "manager": {}, "uni": [], "relay_while_direct": [],
           "long_routes": [], "warn": Counter(), "group_slot": {}}
    for n, lines in logs.items():
        sizes, created, skips, stacks, rst, fatal = Counter(), [], [], defaultdict(list), [], []
        events = []  # ("c", n) created / ("tx", size) / ("skip", toa)
        for t, l in lines:
            if (m := R_TX32.search(l)):
                sizes[int(m.group(1))] += 1
                events.append(("tx", int(m.group(1)), t))
            elif (m := R_CREATED.search(l)):
                created.append(int(m.group(1)))
                events.append(("c", int(m.group(1)), t))
            elif (m := R_SKIP.search(l)):
                skips.append(int(m.group(3)))
                events.append(("skip", int(m.group(3)), t))
            if (m := R_STACK.search(l)):
                stacks[m.group(1)].append(int(m.group(3)))
            if (m := R_RST.search(l)):
                rst.append((t, m.group(2)))
            if R_FATAL.search(l):
                fatal.append(l[:160])
            if (m := R_UNI.search(l)):
                src = phys.get(m.group(1).upper(), m.group(1).upper())
                both = heard_near(n, src, t) and heard_near(src, n, t)
                res["uni"].append({"node": n, "src": src, "t": t, "remote_q": int(m.group(2)), "both_hear": both, "win": inwin(t)})
            if (m := R_ROUTE.search(l)):
                d, v, h = (phys.get(m.group(1).upper(), m.group(1).upper()),
                           phys.get(m.group(2).upper(), m.group(2).upper()), int(m.group(3)))
                if h > 1 and heard_near(n, d, t, 60):
                    res["relay_while_direct"].append({"node": n, "dest": d, "via": v, "hops": h, "t": t, "win": inwin(t)})
                if h >= 3:
                    res["long_routes"].append({"node": n, "dest": d, "via": v, "hops": h, "t": t, "win": inwin(t)})
            if (m := R_WARN.search(l)):
                res["warn"][f"{m.group(1)} {norm(m.group(2))}"] += 1
        # routing skips: a skip right after a Created (routing message attempt) and its follow-up
        r_skips, retried, followed = 0, 0, []
        for i, (k, v, t) in enumerate(events):
            if k != "skip":
                continue
            prev_c = next((e for e in reversed(events[:i]) if e[0] in ("c", "tx")), None)
            if not (prev_c and prev_c[0] == "c" and t - prev_c[2] < 2.0):
                continue  # not a routing message attempt
            r_skips += 1
            nxt_c = next((e for e in events[i + 1:] if e[0] == "c"), None)
            nxt_tx = next((e for e in events[i + 1:] if e[0] in ("tx", "skip")), None)
            same = nxt_c is not None and nxt_c[1] == prev_c[1]
            sent = nxt_tx is not None and nxt_tx[0] == "tx"
            retried += same and sent
            followed.append({"skipped_entries": prev_c[1], "next_entries": nxt_c[1] if nxt_c else None,
                             "next_sent": sent, "gap_s": round(nxt_c[2] - t, 1) if nxt_c else None})
        kmax = max(created, default=0)
        # longest run of routing messages actually sent without a full slice, counted from the
        # first full one (while the table grows, slices are short by design)
        sent = [v for k, v, _ in events if k == "tx"]
        full = max(sent, default=0)
        streak = best = 0
        for v in (sent[sent.index(full):] if full else []):
            streak = streak + 1 if v < full else 0
            best = max(best, streak)
        res["nodes"][n] = {
            "tx32_sizes": dict(sorted(sizes.items())), "tx32_n": sum(sizes.values()),
            "created_entries": dict(sorted(Counter(created).items())), "max_entries": kmax,
            "longest_sent_tail_streak": best, "tx_skips_all": len(skips),
            "tx_skips_routing": r_skips, "routing_skip_retried_same_slice": retried,
            "routing_skip_followups": followed[:20],
            "stack_min_free": {k: min(v) for k, v in stacks.items()},
            "resets": [r for _, r in rst], "fatal": fatal[:5],
        }
        if n != sender and n in act:
            rx = other = 0
            for t, l in lines:
                if not inwin(t):
                    continue
                if "RX:#" in l or "TX:#" in l:
                    ent = re.search(r"(\w+):#\d+\(" + rt[sender] + r"\)", l)
                    if ent:
                        rx += ent.group(1) == "RX"
                        other += ent.group(1) != "RX"
                    else:
                        other += 1
            res["group_slot"][n] = {"sender_RX": rx, "sender_not_RX": other}
    mgr = "006C"
    if mgr in logs:
        mh, sf, last = [], [], None
        for t, l in logs[mgr]:
            if (m := R_MAXH.search(l)):
                mh.append((l[12:24], int(m.group(1))))
            if (m := R_BEACON.search(l)):
                cur = (int(m.group(1)), int(m.group(2)))
                if cur != last:
                    sf.append((l[12:24], *cur))
                    last = cur
        res["manager"] = {"max_hops_updates": mh, "superframe_changes": sf}
    res["warn"] = dict(res["warn"].most_common())
    return res


def group_detail(run):
    from analysis.lm200_metrics import group_stats, _group_members, parse_run
    ev = parse_run(run, window=(None, None))
    mem = _group_members(run)
    gs = group_stats(ev, mem)
    txs = [e for e in ev if e.kind == "group_tx"]
    got = defaultdict(set)
    for e in ev:
        if e.kind == "group_rx":
            got[e.node].add(e.fields["seq"])
    seqs = [e.fields["seq"] for e in txs]
    snd = {e.node for e in txs}
    print(f"group: tx={len(txs)} mean={gs.get('delivery_ratio_mean')} min={gs.get('delivery_ratio_min')} "
          f"dup={gs.get('duplicate_ratio')}")
    for m in sorted((mem or set()) - snd):
        miss = [s for s in seqs if s not in got[m]]
        print(f"   {m}: {len(seqs) - len(miss)}/{len(seqs)}" + (f"  missed seq {miss}" if miss else ""))


def summary(r):
    print(f"\n=== {r['run']}")
    print("node  tx32 sizes                       created entries          maxE tailrun skips(rt/all) retried")
    for n, d in sorted(r["nodes"].items()):
        print(f"{n}  {str(d['tx32_sizes']):32s} {str(d['created_entries']):24s} {d['max_entries']:>3} "
              f"{d['longest_sent_tail_streak']:>4}  {d['tx_skips_routing']:>3}/{d['tx_skips_all']:<3}      "
              f"{d['routing_skip_retried_same_slice']}")
    uni_both = [u for u in r["uni"] if u["both_hear"] and u["win"]]
    print(f"UNIDIRECTIONAL marks: {len(r['uni'])} in the whole log, {sum(u['win'] for u in r['uni'])} in the measurement window, "
          f"of which both ends heard each other's routing (±120 s): {len(uni_both)}")
    for k, c in Counter((u["node"], u["src"]) for u in uni_both).most_common(8):
        print(f"   {k[0]} marked {k[1]} x{c}")
    rw = [x for x in r["relay_while_direct"] if x["win"]]
    print(f"relay routes while dest heard directly (±60 s): {len(r['relay_while_direct'])} whole log, {len(rw)} in window")
    for k, c in Counter((x["node"], x["dest"], x["via"], x["hops"]) for x in rw).most_common(8):
        print(f"   {k[0]}: dest {k[1]} via {k[2]} hops {k[3]} x{c}")
    lw = [x for x in r["long_routes"] if x["win"]]
    print(f"routes with >=3 hops: {len(r['long_routes'])} whole log, {len(lw)} in window")
    for k, c in Counter((x["node"], x["dest"], x["via"], x["hops"]) for x in lw).most_common(6):
        print(f"   {k[0]}: dest {k[1]} via {k[2]} hops {k[3]} x{c}")
    print("manager max_hops:", r["manager"].get("max_hops_updates"))
    print("manager superframe (time, slots, max_hops):", r["manager"].get("superframe_changes"))
    resets = {n: d["resets"] for n, d in r["nodes"].items() if len(d["resets"]) != 1}
    print("nodes with resets != 1:", resets or "none")
    fat = {n: d["fatal"] for n, d in r["nodes"].items() if d["fatal"]}
    print("fatal lines:", fat or "none")
    st = defaultdict(list)
    for d in r["nodes"].values():
        for k, v in d["stack_min_free"].items():
            st[k].append(v)
    print("stack min free (B):", {k: min(v) for k, v in st.items()})
    print("WARN/ERROR:")
    for k, c in list(r["warn"].items())[:15]:
        print(f"   {c:5d} {k}")
    if r["group_slot"]:
        print("sender data-slot entry on members in window (RX / not RX prints):",
              {n: f"{v['sender_RX']}/{v['sender_not_RX']}" for n, v in sorted(r["group_slot"].items())})


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--json")
    ap.add_argument("--sender", default="7B6C", help="group sender (slot-type check)")
    ap.add_argument("--group", action="store_true", help="also print per-member group delivery")
    a = ap.parse_args()
    out = [analyse(Path(r), a.sender.upper()) for r in a.runs]
    for r, rd in zip(out, a.runs):
        summary(r)
        if a.group:
            group_detail(Path(rd))
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1, default=str))
