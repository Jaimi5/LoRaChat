"""Member-sleep metrics (LoRaMesher feat/member-sleep + the LoRaChat sleep firmware).

From one run's full event stream (warmup and cooldown included, like the other
lm200 feature sections), per member and in total:

    deep sleeps      "Deep sleep for N ms" lines; time asleep = sleep line to the
                     DEEPSLEEP_RESET (rst:0x5) that ends it (planned N if none)
    resume success   "Resumed network" / deep sleeps; "Cannot resume" by reason;
                     cold starts after a wake (no "Resumed network" before the
                     node's next sleep or reset)
    sleep clock      |ms off| and the residual error (ms off / ms slept, ppm) of
                     each "Sleep clock:" sample, plus the learned ppm
    boot             rst:0x5 -> BOOT_READY and rst:0x5 -> "Resumed network"
    time-to-ready    rst:0x5 -> "Schedule confirmed by a sync beacon after the resume"
    rejoin signal    join attempts / joins by a member after its first resume
    light sleep      deltas of the HEALTH lsl_n / lsl_ms counters
    late sends       APP_TX_LATE count and late_ms p95

Members are the active non-manager nodes (`members`, default: every node seen)
minus any node that became Network Manager during the run.

Usage:
    python3 scripts/testbed/analysis/sleep_metrics.py <run_dir> [--json out.json]
"""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import Event, parse_run

_JOIN_STATES = {3, 4}
_NM_STATE = 4


def hex4(value) -> str | None:
    """A 16-bit address from config (int 108, '0x006C', '006C', 0) as 'XXXX'.
    None for 0/empty (no fixed manager)."""
    if value is None or value == "":
        return None
    try:
        n = value if isinstance(value, int) else int(str(value), 16)
    except ValueError:
        return None
    return format(n, "04X") if n else None


def _pct(values, q: float):
    if not values:
        return None
    v = sorted(values)
    k = (len(v) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def _dist(values) -> dict:
    xs = [x for x in values if x is not None]
    return {"n": len(xs), "p50": _pct(xs, 0.5), "p95": _pct(xs, 0.95),
            "max": max(xs) if xs else None}


def _is_wake(e: Event) -> bool:
    return (e.kind == "crash" and e.fields.get("kind") == "reset"
            and bool(e.fields.get("deep_sleep_wake")))


def _is_reset(e: Event) -> bool:
    return e.kind == "crash" and e.fields.get("kind") == "reset"


def deep_sleep_intervals(events: list[Event]) -> dict[str, list[dict]]:
    """Per node, every announced deep sleep with what followed it:
    {start, planned_ms, wake (rst:0x5 ts or None), end (wake, else start +
    planned), ready (BOOT_READY after the wake), resumed, confirmed, cold}."""
    by_node: dict[str, list[Event]] = defaultdict(list)
    for e in events:
        by_node[e.node].append(e)
    out: dict[str, list[dict]] = {}
    for node, evs in by_node.items():
        items = []
        cur = None
        for e in evs:
            if e.kind == "deep_sleep":
                cur = {"start": e.ts, "planned_ms": e.fields["sleep_ms"], "wake": None,
                       "ready": None, "resumed": None, "confirmed": None}
                items.append(cur)
                continue
            if cur is None:
                continue
            if _is_reset(e):
                if cur["wake"] is None and _is_wake(e):
                    cur["wake"] = e.ts
                else:
                    cur = None        # another reset: whatever followed is not this sleep's
                continue
            if cur["wake"] is None:
                continue
            if e.kind == "boot_ready" and cur["ready"] is None:
                cur["ready"] = e.ts
            elif e.kind == "resumed_network" and cur["resumed"] is None:
                cur["resumed"] = e.ts
            elif e.kind == "schedule_confirmed" and cur["confirmed"] is None:
                cur["confirmed"] = e.ts
        for it in items:
            it["end"] = it["wake"] if it["wake"] is not None else it["start"] + it["planned_ms"] / 1000.0
            it["cold"] = it["wake"] is not None and it["resumed"] is None
        if items:
            out[node] = items
    return out


def light_sleep_deltas(events: list[Event], window=(None, None)) -> dict[str, dict]:
    """Per node, light-sleep count / ms from consecutive HEALTH lines' cumulative
    lsl_n / lsl_ms (and the firmware's own dsl_n / dsl_ms). A counter that goes
    down restarted at a boot: its new value is the delta. Only intervals whose
    later HEALTH line falls inside `window` are counted."""
    w0, w1 = window
    prev: dict[str, dict] = {}
    out: dict[str, dict] = {}
    for e in events:
        if e.kind != "health" or "lsl_n" not in e.fields:
            continue
        f = e.fields
        p = prev.get(e.node)
        prev[e.node] = f
        if p is None:
            continue
        if (w0 is not None and e.ts < w0) or (w1 is not None and e.ts > w1):
            continue
        acc = out.setdefault(e.node, {"lsl_n": 0, "lsl_ms": 0, "dsl_n": 0, "dsl_ms": 0, "samples": 0})
        acc["samples"] += 1
        for k in ("lsl_n", "lsl_ms", "dsl_n", "dsl_ms"):
            if k in f:
                cur, before = f[k], p.get(k, 0)
                acc[k] += cur - before if cur >= before else cur
    return out


def sleep_stats(events: list[Event], superframe_s: float | None = None,
                members: set[str] | None = None) -> dict:
    kinds = Counter(e.kind for e in events)
    sleep_kinds = ("deep_sleep", "resumed_network", "sleep_clock", "cannot_resume",
                   "sleepcb", "boot", "boot_ready", "app_tx_late", "no_deep_sleep")
    has_lsl = any(e.kind == "health" and "lsl_n" in e.fields for e in events)
    if not any(kinds.get(k) for k in sleep_kinds) and not has_lsl:
        return {"present": False}

    nm_nodes = {e.node for e in events if e.kind == "state_change"
                and e.fields.get("state") == _NM_STATE and not e.fields.get("resume")}
    seen = {e.node for e in events}
    members = set(members) if members is not None else set(seen)
    members -= nm_nodes

    intervals = deep_sleep_intervals(events)
    sleeps = [it for n, lst in intervals.items() for it in lst]
    wakes = [e for e in events if _is_wake(e)]
    unannounced = [e for e in events if _is_reset(e) and not e.fields.get("deep_sleep_wake")
                   and str(e.fields.get("code", "")).lower() == "0x5"]
    resumed = [e for e in events if e.kind == "resumed_network"]
    cannot = [e for e in events if e.kind == "cannot_resume"]

    # Node log spans (asleep fraction denominator).
    span: dict[str, list[float]] = {}
    for e in events:
        s = span.setdefault(e.node, [e.ts, e.ts])
        s[1] = max(s[1], e.ts)
    for n, lst in intervals.items():
        span[n][1] = max(span[n][1], max(it["end"] for it in lst))

    # Join activity by a member after its first resume (the resume failed, or
    # the member lost the network anyway): failure signal.
    first_resume: dict[str, float] = {}
    for e in resumed:
        first_resume.setdefault(e.node, e.ts)
    join_attempts: Counter = Counter()
    rejoins: Counter = Counter()
    joined_state: dict[str, bool] = {}
    for e in events:
        if e.kind == "state_change" and not e.fields.get("resume"):
            now = e.fields.get("state") in _JOIN_STATES
            t0 = first_resume.get(e.node)
            if now and not joined_state.get(e.node, False) and t0 is not None and e.ts > t0:
                rejoins[e.node] += 1
            joined_state[e.node] = now
        elif e.kind in ("join_scheduled", "join_unanswered"):
            t0 = first_resume.get(e.node)
            if t0 is not None and e.ts > t0:
                join_attempts[e.node] += 1
        elif _is_reset(e) and not e.fields.get("deep_sleep_wake"):
            joined_state[e.node] = False

    clock = [e for e in events if e.kind == "sleep_clock"]

    def clock_summary(samples) -> dict:
        return {
            "n": len(samples),
            "abs_off_ms": _dist([abs(e.fields["off_ms"]) for e in samples]),
            "abs_residual_ppm": _dist([abs(e.fields["off_ms"]) / e.fields["slept_ms"] * 1e6
                                       for e in samples if e.fields["slept_ms"]]),
            "abs_learned_ppm": _dist([abs(e.fields["ppm"]) for e in samples]),
        }

    boot_ms = [(it["ready"] - it["wake"]) * 1000.0 for it in sleeps
               if it["wake"] is not None and it["ready"] is not None]
    to_resumed_ms = [(it["resumed"] - it["wake"]) * 1000.0 for it in sleeps
                     if it["wake"] is not None and it["resumed"] is not None]
    ready_ms = [(it["confirmed"] - it["wake"]) * 1000.0 for it in sleeps
                if it["wake"] is not None and it["confirmed"] is not None]
    ready = _dist(ready_ms)
    if superframe_s and ready["p95"] is not None:
        ready["p95_superframes"] = ready["p95"] / 1000.0 / superframe_s

    light = light_sleep_deltas(events)
    late = [e.fields["late_ms"] for e in events if e.kind == "app_tx_late"]
    late_by_node: dict[str, list[int]] = defaultdict(list)
    for e in events:
        if e.kind == "app_tx_late":
            late_by_node[e.node].append(e.fields["late_ms"])
    sends = Counter(e.node for e in events if e.kind == "app_tx")

    per_node = {}
    for n in sorted(seen):
        lst = intervals.get(n, [])
        deep_ms = sum((it["end"] - it["start"]) * 1000.0 for it in lst)
        sp = span.get(n)
        dur_ms = (sp[1] - sp[0]) * 1000.0 if sp else 0.0
        ls = light.get(n, {})
        if not lst and not ls and n not in first_resume and n not in late_by_node:
            continue
        per_node[n] = {
            "member": n in members,
            "deep_sleeps": len(lst),
            "deep_ms": round(deep_ms),
            "asleep_frac": (deep_ms / dur_ms) if dur_ms else None,
            "resumed": sum(1 for it in lst if it["resumed"] is not None),
            "cold_after_wake": sum(1 for it in lst if it["cold"]),
            "light_n": ls.get("lsl_n"),
            "light_ms": ls.get("lsl_ms"),
            "join_attempts_after_resume": join_attempts.get(n, 0),
            "rejoins_after_resume": rejoins.get(n, 0),
            "app_tx_late": len(late_by_node.get(n, [])),
        }

    n_sleeps = len(sleeps)
    deep_members = sorted(n for n in members if intervals.get(n))
    light_members = sorted(n for n in members if (light.get(n) or {}).get("lsl_n", 0) > 0)
    member_frac = [per_node[n]["asleep_frac"] for n in members
                   if n in per_node and per_node[n]["asleep_frac"] is not None]
    return {
        "present": True,
        "members": sorted(members),
        "nm_nodes": sorted(nm_nodes),
        "n_members": len(members),
        "n_members_deep_slept": len(deep_members),
        "frac_members_deep_slept": (len(deep_members) / len(members)) if members else None,
        "members_without_deep_sleep": sorted(members - set(deep_members)),
        "n_deep_sleeps": n_sleeps,
        "deep_ms_total": round(sum((it["end"] - it["start"]) * 1000.0 for it in sleeps)),
        "asleep_frac_member_mean": (sum(member_frac) / len(member_frac)) if member_frac else None,
        "n_wakes": len(wakes),
        "n_unannounced_deepsleep_resets": len(unannounced),
        "n_resumed": len(resumed),
        "resume_success": (len(resumed) / n_sleeps) if n_sleeps else None,
        "n_cannot_resume": len(cannot),
        "cannot_resume_frac": (len(cannot) / n_sleeps) if n_sleeps else None,
        "cannot_resume_by_reason": dict(Counter(e.fields["category"] for e in cannot)),
        "cold_starts_after_wake": sum(1 for it in sleeps if it["cold"]),
        "n_resume_state_invalid": kinds.get("resume_state_invalid", 0),
        "n_no_saved_state": kinds.get("no_saved_state", 0),
        "n_saved_state": kinds.get("saved_state", 0),
        "no_deep_sleep_reasons": dict(Counter(e.fields["reason"] for e in events
                                              if e.kind == "no_deep_sleep").most_common()),
        # All samples, plus per kind: the library calibrates light and deep sleep apart.
        "sleep_clock": {
            **clock_summary(clock),
            "by_kind": {k: clock_summary([e for e in clock if e.fields.get("sleep_kind") == k])
                        for k in ("light", "deep")},
        },
        "boot_ms": _dist(boot_ms),
        "wake_to_resumed_ms": _dist(to_resumed_ms),
        "time_to_ready_ms": ready,
        "join_attempts_after_resume": sum(join_attempts.values()),
        "rejoins_after_resume": sum(rejoins.values()),
        "rejoined_members": sorted(n for n in rejoins if n in members),
        "light_sleep": {
            "present": has_lsl,
            "n_total": sum(v["lsl_n"] for v in light.values()),
            "ms_total": sum(v["lsl_ms"] for v in light.values()),
            "members_with_light_sleep": light_members,
            "members_without_light_sleep": sorted(members - set(light_members)) if has_lsl else None,
            "firmware_deep_n_total": sum(v["dsl_n"] for v in light.values()),
        },
        "app_tx_late": {"n": len(late), "n_app_tx": sum(sends.values()),
                        "frac_of_sends": (len(late) / sum(sends.values())) if sends else None,
                        "p50_ms": _pct(late, 0.5), "p95_ms": _pct(late, 0.95),
                        "max_ms": max(late) if late else None,
                        "per_node": {n: len(v) for n, v in sorted(late_by_node.items())}},
        "boot_reasons": dict(Counter(str(e.fields["reason"]) for e in events if e.kind == "boot")),
        "per_node": per_node,
    }


def analyse(run_dir: Path) -> dict:
    run_dir = Path(run_dir)
    events = parse_run(run_dir, window=(None, None))
    from analysis.lm200_metrics import _sleep_members, _superframe_s
    return sleep_stats(events, superframe_s=_superframe_s(run_dir),
                       members=_sleep_members(run_dir))


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Member-sleep metrics for one run")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--json", type=Path, default=None)
    args = ap.parse_args()
    text = json.dumps(analyse(args.run_dir), indent=2, sort_keys=True, default=str)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(text)
        print(f"wrote {args.json}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
