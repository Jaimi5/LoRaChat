#!/usr/bin/env python3
"""AP8 attended deep-sleep smoke: print every check for one run (batches/lm200s_smoke_deep.yaml).

    python3 scripts/testbed/lm200/smoke_deep_sleep.py runs/lm200s_smoke_deep__slp/<run> [--json out.json]

Sections (PASS / FAIL / INFO; exit 0 only when nothing FAILs):
  build      every board flashed, LoRaMesher sha (default 86a4072), RadioLib 7.7.1,
             every board reports the expected build env (ttgo-t-beam-v2-sleep)
  validity   runner/run_checks.py: host suspend, window, log gap, config leak
  leak drill the real CFG lines against a copy of config.yaml with one board's own
             CFG-checked override removed (7984 packet_delay in lm200_smoke_leak.yaml):
             must flag exactly that board
  sleep      analysis/sleep_metrics.py: members deep-slept, resume success, Cannot
             resume, cold starts, sleep clock, time-to-ready, rejoins after a resume
  boot       LORA_DEEP_SLEEP_BOOT_MS calibration, per deep-sleep wake: rst:0x5 -> BOOT,
             rst:0x5 -> BOOT_READY, BOOT -> BOOT_READY (host timestamps) and the
             device's BOOT_READY app_ms; p50 / p95 / max and a suggested value
  radio      after every resumed wake the node heard the network again (sync-beacon
             confirmation or a received packet) before its next sleep
  app seq    APP_TX seq per sender counts up by one across wakes (no reset, no gap)
  health     HEALTH (with lsl_/dsl_ counters) on every active board; no unexpected
             reboot (rst:0x5 after an announced sleep is a wake, not a reboot); no panic
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
TESTBED = HERE.parent
sys.path.insert(0, str(TESTBED))
sys.path.insert(0, str(HERE))

import yaml  # noqa: E402

import gates  # noqa: E402
from analysis import app_pdr, lm200_metrics, sleep_metrics  # noqa: E402
from analysis.parse_logs import parse_run  # noqa: E402
from runner import run_checks  # noqa: E402

PASS, FAIL, INFO = "PASS", "FAIL", "INFO"


def _dist(xs):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return {"n": 0, "p50": None, "p95": None, "max": None}

    def q(p):
        k = (len(xs) - 1) * p
        lo, hi = int(k), min(int(k) + 1, len(xs) - 1)
        return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)
    return {"n": len(xs), "p50": round(q(0.5)), "p95": round(q(0.95)), "max": round(xs[-1])}


class Report:
    def __init__(self):
        self.rows = []

    def add(self, section, name, status, detail=""):
        self.rows.append({"section": section, "check": name, "status": status, "detail": detail})
        print(f"  [{status:<4}] {section:<10} {name:<28} {detail}")

    @property
    def failed(self):
        return [r for r in self.rows if r["status"] == FAIL]


def per_node(events):
    out = defaultdict(list)
    for e in events:
        out[e.node].append(e)
    return out


def boot_samples(events):
    """Per deep-sleep wake: (node, ms rst->BOOT, ms rst->BOOT_READY, ms BOOT->BOOT_READY, app_ms)."""
    rows = []
    for node, evs in per_node(events).items():
        wake = boot = None
        for e in evs:
            if e.kind == "crash" and e.fields.get("kind") == "reset":
                wake = e.ts if e.fields.get("deep_sleep_wake") else None
                boot = None
            elif wake is not None and e.kind == "boot" and boot is None:
                boot = e.ts
            elif wake is not None and e.kind == "boot_ready":
                rows.append({"node": node, "rst_to_boot_ms": (boot - wake) * 1000 if boot else None,
                             "rst_to_ready_ms": (e.ts - wake) * 1000,
                             "boot_to_ready_ms": (e.ts - boot) * 1000 if boot else None,
                             "ready_app_ms": e.fields["app_ms"]})
                wake = None
    return rows


def radio_after_wake(events):
    """(resumed wakes, wakes that heard the network before the node's next sleep/reset)."""
    resumed = heard = 0
    for node, evs in per_node(events).items():
        state = None   # None | "woken" | "resumed" | "heard"
        for e in evs:
            if e.kind == "crash" and e.fields.get("kind") == "reset":
                state = "woken" if e.fields.get("deep_sleep_wake") else None
            elif e.kind == "deep_sleep":
                state = None
            elif state == "woken" and e.kind == "resumed_network":
                state = "resumed"
                resumed += 1
            elif state == "resumed" and e.kind in ("schedule_confirmed", "pkt_rx"):
                state = "heard"
                heard += 1
    return resumed, heard


def seq_continuity(events):
    by = defaultdict(list)
    for e in events:
        if e.kind == "app_tx":
            by[e.node].append(e.fields["seq"])
    out = {}
    for node, seqs in sorted(by.items()):
        resets = sum(1 for a, b in zip(seqs, seqs[1:]) if b <= a)
        gaps = sum(b - a - 1 for a, b in zip(seqs, seqs[1:]) if b > a + 1)
        out[node] = {"n": len(seqs), "first": seqs[0], "last": seqs[-1], "resets": resets, "gaps": gaps}
    return out


def leak_drill(run_dir: Path, cfg_h: str, node: str | None, key: str | None):
    """Copy config.yaml (logs symlinked) with one board's own override removed."""
    raw = yaml.safe_load((run_dir / "config.yaml").read_text())
    keys = {k for k, _ in run_checks.CFG_FIELDS.values()}
    devs = {str(k).upper(): (v or {}) for k, v in (raw.get("devices") or {}).items()}
    if node is None:
        cands = [(n, k) for n, d in sorted(devs.items()) for k in d if k in keys
                 and str(d.get("node_active", 1)) != "0"]
        if not cands:
            return None, "no board has its own CFG-checked override to drop (see lm200_smoke_leak.yaml)"
        node, key = cands[0]
    orig = next(k for k in raw["devices"] if str(k).upper() == node)
    if key not in (raw["devices"][orig] or {}):
        return None, f"{node} has no own {key} in config.yaml"
    val = raw["devices"][orig].pop(key)
    tmp = Path(tempfile.mkdtemp(prefix="leakdrill-")) / run_dir.parent.name / run_dir.name
    tmp.mkdir(parents=True)
    (tmp / "config.yaml").write_text(yaml.safe_dump(raw, sort_keys=False))
    (tmp / "logs").symlink_to((run_dir / "logs").resolve())
    ok, det = run_checks.config_leak_check(tmp, cfg_h)
    shutil.rmtree(tmp.parent.parent)
    flagged = sorted({part.split("#")[0].strip() for part in det.split(":", 1)[-1].split(";")
                      if "#" in part}) if ok is False else []
    return (ok is False and flagged == [node]), (f"dropped {node} {key}={val}: check says {det[:300]}"
                                                 + ("" if flagged in ([node], []) else f"; flagged {flagged}"))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--expect-sha", default="86a4072")
    ap.add_argument("--expect-radiolib", default="7.7.1")
    ap.add_argument("--expect-env", default="ttgo-t-beam-v2-sleep")
    ap.add_argument("--leak-node", default=None, help="default: the first board with its own CFG key")
    ap.add_argument("--leak-key", default=None)
    ap.add_argument("--json", type=Path, default=None)
    a = ap.parse_args(argv)
    run = gates.tpath(a.run_dir).resolve()
    rep = Report()
    print(f"AP8 deep-sleep smoke: {run.name}")
    cfg = gates.run_config(run)
    active = set(cfg["active"])
    cfg_h = run_checks.committed_config_h()

    # build
    up = gates.scan_upload_logs(run)
    scan = gates.scan_device_logs(run)
    if up is None:
        rep.add("build", "flash", FAIL, "no GW-*-upload.log in logs/")
    else:
        miss = sorted(set(cfg["devices"]) - set(up["ok"]))
        rep.add("build", "flash", FAIL if miss or up["failed"] else PASS,
                f"{len(up['ok'])}/{len(cfg['devices'])} OK" + (f"; failed {up['failed'] or miss}" if miss else ""))
        bad = [v for v in up["lib_versions"] if not v.split("+sha.")[-1].startswith(a.expect_sha)]
        rep.add("build", "lib_sha", FAIL if bad or not up["lib_versions"] else PASS,
                f"{sorted(up['lib_versions'])} want {a.expect_sha}")
        probs = gates.radiolib_problems(up, a.expect_radiolib)
        rep.add("build", "radiolib", FAIL if probs else PASS, "; ".join(probs) or a.expect_radiolib)
    envs = {n: d["build_env"] for n, d in scan.items()}
    wrong = {n: e for n, e in envs.items() if e and e != a.expect_env}
    rep.add("build", "build_env", FAIL if wrong else PASS, f"wrong {wrong}" if wrong else a.expect_env)

    # validity
    for r in run_checks.check_run(run, cfg_h):
        st = {True: PASS, False: FAIL, None: FAIL if r["check"] == "config_leak" else INFO}[r["ok"]]
        rep.add("validity", r["check"], st, r["detail"][:300])

    # leak drill
    ok, det = leak_drill(run, cfg_h, a.leak_node.upper() if a.leak_node else None, a.leak_key)
    rep.add("leak", "cfg_check_catches_leak", PASS if ok else FAIL, det)

    # sleep
    events = parse_run(run, window=(None, None))
    sl = sleep_metrics.sleep_stats(events, superframe_s=lm200_metrics._superframe_s(run),
                                   members=lm200_metrics._sleep_members(run))
    if not sl.get("present") or not sl.get("n_deep_sleeps"):
        rep.add("sleep", "deep_sleeps", FAIL, "no 'Deep sleep for' line")
    else:
        fr = sl["frac_members_deep_slept"] or 0
        rep.add("sleep", "members_deep_slept", PASS if fr >= 0.8 else FAIL,
                f"{sl['n_members_deep_slept']}/{sl['n_members']}; none on {sl['members_without_deep_sleep']}")
        rep.add("sleep", "deep_sleeps", INFO, f"{sl['n_deep_sleeps']} sleeps, {sl['deep_ms_total'] / 1000:.0f} s, "
                f"member asleep fraction {sl['asleep_frac_member_mean']:.2f}"
                if sl["asleep_frac_member_mean"] is not None else f"{sl['n_deep_sleeps']} sleeps")
        rs = sl["resume_success"] or 0
        rep.add("sleep", "resume_success", PASS if rs >= 0.95 else FAIL,
                f"{sl['n_resumed']}/{sl['n_deep_sleeps']} = {rs:.3f}; cold starts after a wake "
                f"{sl['cold_starts_after_wake']}")
        rep.add("sleep", "cannot_resume", PASS if (sl["cannot_resume_frac"] or 0) <= 0.05 else FAIL,
                f"{sl['n_cannot_resume']} {sl['cannot_resume_by_reason']}")
        ppm = sl["sleep_clock"]["abs_residual_ppm"]
        rep.add("sleep", "sleep_clock_ppm", INFO if ppm["p95"] is None else (PASS if ppm["p95"] <= 10000 else FAIL),
                f"residual |ppm| {ppm}; |ms off| {sl['sleep_clock']['abs_off_ms']}")
        tr = sl["time_to_ready_ms"]
        rep.add("sleep", "time_to_ready", INFO, f"rst:0x5 -> sync-beacon confirmation {tr}")
        rep.add("sleep", "rejoins_after_resume", FAIL if sl["rejoined_members"] else PASS,
                f"{sl['rejoined_members']}; join attempts {sl['join_attempts_after_resume']}")
        rep.add("sleep", "no_deep_sleep_reasons", INFO, json.dumps(sl["no_deep_sleep_reasons"])[:200])
        rep.add("sleep", "app_tx_late", INFO, json.dumps({k: sl["app_tx_late"][k] for k in ("n", "p95_ms", "max_ms")}))

    # boot calibration
    bs = boot_samples(events)
    budget = cfg.get("boot_budget_ms") or gates._config_h_define("LORA_DEEP_SLEEP_BOOT_MS")
    d_ready = _dist([b["rst_to_ready_ms"] for b in bs])
    for name, key in (("rst_to_BOOT_ms", "rst_to_boot_ms"), ("rst_to_BOOT_READY_ms", "rst_to_ready_ms"),
                      ("BOOT_to_BOOT_READY_ms", "boot_to_ready_ms"), ("BOOT_READY_app_ms", "ready_app_ms")):
        rep.add("boot", name, INFO, json.dumps(_dist([b[key] for b in bs])))
    if d_ready["n"]:
        suggest = int(math.ceil(d_ready["max"] * 1.2 / 100.0) * 100)
        st = PASS if budget and d_ready["max"] <= budget else FAIL
        rep.add("boot", "LORA_DEEP_SLEEP_BOOT_MS", st,
                f"current {budget} ms vs worst rst->BOOT_READY {d_ready['max']} ms; "
                f"suggest {suggest} ms (worst x1.2, rounded up to 100 ms)")
    else:
        rep.add("boot", "LORA_DEEP_SLEEP_BOOT_MS", FAIL, "no deep-sleep wake followed by BOOT_READY")

    # radio rail after wake
    resumed, heard = radio_after_wake(events)
    rep.add("radio", "heard_network_after_wake", PASS if resumed and heard == resumed else FAIL,
            f"{heard}/{resumed} resumed wakes got a sync-beacon confirmation or a packet before the next sleep")

    # app seq
    seqs = seq_continuity(events)
    bad = {n: v for n, v in seqs.items() if v["resets"] or v["gaps"]}
    rep.add("app_seq", "continuous_across_wakes", FAIL if bad or not seqs else PASS,
            f"{len(seqs)} senders; " + (f"resets/gaps {bad}" if bad else "every sender counts up by one"))
    app = app_pdr.analyse(run)
    rep.add("app_seq", "fair_pdr", INFO, f"{gates.dig(app, 'totals.pdr_delivered_intended')} "
            f"(delivered {gates.dig(app, 'totals.delivered')} / intended {gates.dig(app, 'totals.intended')})")

    # health / reboots
    hl = defaultdict(lambda: [0, 0])
    for e in events:
        if e.kind == "health":
            hl[e.node][0] += 1
            hl[e.node][1] += "lsl_n" in e.fields
    no_h = sorted(active - set(hl))
    no_c = sorted(n for n in active if hl.get(n, [0, 0])[1] == 0)
    rep.add("health", "health_lines", FAIL if no_h or no_c else PASS,
            f"on {len(set(hl) & active)}/{len(active)} active boards" + (f"; none on {no_h}" if no_h else "")
            + (f"; no sleep counters on {no_c}" if no_c else ""))
    cs = lm200_metrics.crash_stats(events)
    wakes = {n: d["deep_sleep_wakes"] for n, d in scan.items() if d["deep_sleep_wakes"]}
    unexp = {n: d["unexpected_reboots"] for n, d in scan.items() if d["unexpected_reboots"]}
    rep.add("health", "deepsleep_reset_not_flagged", FAIL if cs["unexpected_resets_total"] or unexp else PASS,
            f"deep-sleep wakes {cs['deep_sleep_wakes_total']} ({wakes}); unexpected reboots "
            f"{cs['unexpected_resets']} / raw scan {unexp}; unannounced rst:0x5 {sl.get('n_unannounced_deepsleep_resets')}")
    rep.add("health", "panics", FAIL if cs["panic_markers_total"] else PASS, json.dumps(cs["panic_markers"])[:200])

    n_fail = len(rep.failed)
    print(f"\n{'SMOKE PASS' if not n_fail else f'SMOKE FAIL ({n_fail} check(s))'}")
    if a.json:
        a.json.write_text(json.dumps({"run": str(run), "checks": rep.rows, "boot_samples": bs,
                                      "seq": seqs, "sleep": sl}, indent=2, default=str))
    return 1 if n_fail else 0


if __name__ == "__main__":
    raise SystemExit(main())
