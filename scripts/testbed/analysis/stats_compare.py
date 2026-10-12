#!/usr/bin/env python3
"""Statistical comparison of the lm200-sleep campaign arms (Welch t-test, TOST).

The campaign gates compare medians against fixed margins with n = 1-3 runs per
arm, so a GREY or FAIL can be run-to-run noise. This script repeats each
comparison with:

  - Welch's t-test (unequal variances) on the per-run metric, with the 95 % CI
    of the difference and Hedges' g;
  - Mann-Whitney U as a rank-based check;
  - a paired t-test where the same nodes are measured in both arms;
  - TOST equivalence (two one-sided Welch tests) against the gate's own margin,
    because "no significant difference" alone is not evidence of equivalence.

Per-message / per-node / per-cycle samples give more power but are clustered
within runs (not independent); they are reported as secondary evidence and the
per-run test is the primary one.

Usage:
    python3 scripts/testbed/analysis/stats_compare.py [--out-md stats.md] [--out-json stats.json]
"""
from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
from collections import defaultdict, deque
from pathlib import Path

import numpy as np
from scipy import stats

HERE = Path(__file__).resolve().parent
TESTBED = HERE.parent
sys.path.insert(0, str(TESTBED))

from analysis import app_pdr, formation, lm200_metrics  # noqa: E402
from analysis.parse_logs import parse_run  # noqa: E402

RUNS = TESTBED / "runs"
RUN_RE = re.compile(r"-r\d\d$")          # skips .FAILED / .G0FAIL / superseded dirs
REMOTE = {"3428", "14A4", "DD58"}
NM = "006C"
WARM_NODES = {"7B6C", "B4DC"}            # sim_stopstart=2 (SaveState before Stop) in S4


# ── statistics ───────────────────────────────────────────────────────────────

def welch(a: list[float], b: list[float]) -> dict:
    """Welch's t-test of mean(b) - mean(a), with 95 % CI and Hedges' g."""
    a, b = [float(x) for x in a if x is not None], [float(x) for x in b if x is not None]
    out = {"n_a": len(a), "n_b": len(b),
           "mean_a": statistics.fmean(a) if a else None,
           "mean_b": statistics.fmean(b) if b else None}
    if len(a) < 2 or len(b) < 2:
        out["note"] = "n < 2 in one arm: no test"
        return out
    va, vb = statistics.variance(a), statistics.variance(b)
    se2 = va / len(a) + vb / len(b)
    diff = out["mean_b"] - out["mean_a"]
    out["diff"] = diff
    out["sd_a"], out["sd_b"] = math.sqrt(va), math.sqrt(vb)
    if se2 == 0:
        out["note"] = "zero variance in both arms"
        out["p"] = 1.0 if diff == 0 else 0.0
        return out
    df = se2 ** 2 / ((va / len(a)) ** 2 / (len(a) - 1) + (vb / len(b)) ** 2 / (len(b) - 1))
    t = diff / math.sqrt(se2)
    out.update(t=t, df=df, p=2 * stats.t.sf(abs(t), df))
    half = stats.t.ppf(0.975, df) * math.sqrt(se2)
    out["ci95"] = (diff - half, diff + half)
    sp = math.sqrt(((len(a) - 1) * va + (len(b) - 1) * vb) / (len(a) + len(b) - 2))
    if sp > 0:
        j = 1 - 3 / (4 * (len(a) + len(b)) - 9)          # small-sample correction
        out["hedges_g"] = diff / sp * j
    if len(a) >= 3 and len(b) >= 3:
        out["mw_p"] = stats.mannwhitneyu(a, b, alternative="two-sided").pvalue
    return out


def tost(a: list[float], b: list[float], margin: float) -> dict:
    """Two one-sided Welch tests: H0 |mean(b) - mean(a)| >= margin."""
    a, b = [float(x) for x in a if x is not None], [float(x) for x in b if x is not None]
    if len(a) < 2 or len(b) < 2:
        return {"margin": margin, "note": "n < 2 in one arm"}
    va, vb = statistics.variance(a), statistics.variance(b)
    se2 = va / len(a) + vb / len(b)
    diff = statistics.fmean(b) - statistics.fmean(a)
    if se2 == 0:
        return {"margin": margin, "p": 0.0 if abs(diff) < margin else 1.0, "equivalent": abs(diff) < margin}
    df = se2 ** 2 / ((va / len(a)) ** 2 / (len(a) - 1) + (vb / len(b)) ** 2 / (len(b) - 1))
    se = math.sqrt(se2)
    p_low = stats.t.sf((diff + margin) / se, df)      # H0: diff <= -margin
    p_high = stats.t.cdf((diff - margin) / se, df)    # H0: diff >= +margin
    p = max(p_low, p_high)
    return {"margin": margin, "p": p, "equivalent": p < 0.05}


def paired(a: dict[str, float], b: dict[str, float], margin: float | None = None) -> dict:
    """Paired t-test over the keys present in both (e.g. per node), plus a
    paired TOST against `margin` when given."""
    keys = sorted(set(a) & set(b))
    x = [a[k] for k in keys]
    y = [b[k] for k in keys]
    out = {"n_pairs": len(keys)}
    if len(keys) < 3:
        out["note"] = "fewer than 3 pairs"
        return out
    d = [yy - xx for xx, yy in zip(x, y)]
    out["mean_diff"] = statistics.fmean(d)
    if statistics.pstdev(d) == 0:
        out["p"] = 1.0 if out["mean_diff"] == 0 else 0.0
        out["note"] = "identical differences"
        return out
    r = stats.ttest_rel(y, x)
    out["t"], out["p"] = float(r.statistic), float(r.pvalue)
    se = statistics.stdev(d) / math.sqrt(len(d))
    half = stats.t.ppf(0.975, len(d) - 1) * se
    out["ci95"] = (out["mean_diff"] - half, out["mean_diff"] + half)
    if margin is not None:
        df = len(d) - 1
        p_tost = max(stats.t.sf((out["mean_diff"] + margin) / se, df),
                     stats.t.cdf((out["mean_diff"] - margin) / se, df))
        out["tost"] = {"margin": margin, "p": float(p_tost), "equivalent": p_tost < 0.05}
    if len(keys) >= 6 and any(d):
        try:
            out["wilcoxon_p"] = float(stats.wilcoxon(y, x).pvalue)
        except ValueError:
            pass
    return out


# ── run discovery and metric extraction ──────────────────────────────────────

def runs(batch: str, cell: str) -> list[Path]:
    d = RUNS / batch
    if not d.is_dir():
        return []
    return sorted(p for p in d.iterdir() if p.is_dir() and RUN_RE.search(p.name) and f"-{cell}-r" in p.name
                  and any((p / "logs").glob("monitor-dev-*")))     # skips runs still in progress


_EV_CACHE: dict[Path, list] = {}


def events(run: Path):
    if run not in _EV_CACHE:
        _EV_CACHE[run] = sorted(parse_run(run, window=(None, None)), key=lambda e: e.ts)
    return _EV_CACHE[run]


def app_latencies_ms(run: Path) -> list[float]:
    """Per-message APP_TX -> APP_RX latency (FIFO per (src, seq), like app_pdr.compute)."""
    pending: dict[tuple, deque] = defaultdict(deque)
    out = []
    for e in events(run):
        if e.kind == "app_tx":
            pending[(e.fields["src"], e.fields["seq"])].append(e.ts)
        elif e.kind == "app_rx" and e.fields.get("app", app_pdr.SIM_APP) == app_pdr.SIM_APP:
            q = pending.get((e.fields["src"], e.fields["seq"]))
            if q:
                out.append((e.ts - q.popleft()) * 1000.0)
    return out


def app_latencies_from_schedule_ms(run: Path) -> list[float]:
    """Latency from the scheduled send time: APP_TX -> APP_RX plus the APP_TX_LATE
    delay of that (src, seq). With sim_send_path 1 a send that waited for the node
    to wake or for a free slot is logged as APP_TX only when it goes out, so the
    plain latency leaves the wait out."""
    late = {(e.fields["src"], e.fields["seq"]): float(e.fields["late_ms"])
            for e in events(run) if e.kind == "app_tx_late"}
    pending: dict[tuple, deque] = defaultdict(deque)
    out = []
    for e in events(run):
        if e.kind == "app_tx":
            pending[(e.fields["src"], e.fields["seq"])].append(e.ts)
        elif e.kind == "app_rx" and e.fields.get("app", app_pdr.SIM_APP) == app_pdr.SIM_APP:
            k = (e.fields["src"], e.fields["seq"])
            q = pending.get(k)
            if q:
                out.append((e.ts - q.popleft()) * 1000.0 + late.get(k, 0.0))
    return out


def app_summary(run: Path) -> dict:
    return app_pdr.analyse(run)


def per_node_pdr(run: Path) -> dict[str, float]:
    flows = app_summary(run).get("pdr_per_flow") or {}
    return {n: f["pdr"] for n, f in flows.items() if f.get("sent")}


def formation_from_reset(run: Path) -> dict:
    return formation.analyse(run, window="run")


def main_cluster_last_join(run: Path) -> float | None:
    j = formation_from_reset(run).get("joined_since_reset_s") or {}
    v = [t for n, t in j.items() if t is not None and n not in REMOTE and n != NM]
    return max(v) if v else None


def main_cluster_join_times(run: Path) -> list[float]:
    j = formation_from_reset(run).get("joined_since_reset_s") or {}
    return [t for n, t in j.items() if t is not None and n not in REMOTE and n != NM]


def stopstart_cycles(run: Path) -> list[dict]:
    return lm200_metrics.compute_all(run).get("stopstart", {}).get("cycles", [])


def reliable(run: Path) -> dict:
    return lm200_metrics.compute_all(run).get("reliable", {})


def reliable_rtts_ms(run: Path) -> list[float]:
    return [float(e.fields["rtt"]) for e in events(run) if e.kind == "app_ack"]


# ── report helpers ───────────────────────────────────────────────────────────

def fmt(x, nd=3):
    if x is None:
        return "–"
    if isinstance(x, tuple):
        return f"[{fmt(x[0], nd)}, {fmt(x[1], nd)}]"
    if isinstance(x, float):
        if abs(x) >= 1000:
            return f"{x:.0f}"
        return f"{x:.{nd}g}"
    return str(x)


def verdict(w: dict, eq: dict | None = None) -> str:
    p = w.get("p")
    if p is None:
        return w.get("note", "no test")
    s = "significant (p<0.05)" if p < 0.05 else "not significant"
    if eq is not None and "p" in eq:
        s += "; equivalent within ±" + fmt(eq["margin"]) if eq.get("equivalent") else \
             "; equivalence not shown (±" + fmt(eq["margin"]) + ")"
    return s


ROWS: list[dict] = []


def add(section: str, metric: str, level: str, a_label: str, b_label: str, w: dict,
        eq: dict | None = None, unit: str = "", note: str = ""):
    ROWS.append({"section": section, "metric": metric, "level": level, "a": a_label, "b": b_label,
                 "welch": w, "tost": eq, "unit": unit, "note": note})


def add_paired(section: str, metric: str, a_label: str, b_label: str, pr: dict, unit="", note=""):
    ROWS.append({"section": section, "metric": metric, "level": "per node (paired)", "a": a_label,
                 "b": b_label, "paired": pr, "unit": unit, "note": note})


# ── comparisons ──────────────────────────────────────────────────────────────

def ab_13node(cell: str, label: str, superframe_s: float):
    fix, slp = runs("lm200s_13node__fix", cell), runs("lm200s_13node__slp", cell)
    if not fix or not slp:
        return
    sec = f"FIX vs SLP, 13 nodes {label}"
    pf = [app_summary(r)["totals"]["pdr_delivered_intended"] for r in fix]
    ps = [app_summary(r)["totals"]["pdr_delivered_intended"] for r in slp]
    add(sec, "fair PDR", "per run", "FIX", "SLP", welch(pf, ps), tost(pf, ps, 0.02),
        note="TOST margin = ab gate pdr_drop 0.02")
    cf = [formation_from_reset(r).get("network_convergence_since_reset_s") for r in fix]
    cs = [formation_from_reset(r).get("network_convergence_since_reset_s") for r in slp]
    add(sec, "convergence from reset", "per run", "FIX", "SLP", welch(cf, cs),
        tost(cf, cs, superframe_s), unit="s",
        note=f"TOST margin = 1 superframe ({superframe_s:.1f} s, ab gate conv_superframes)")
    jf = [main_cluster_last_join(r) for r in fix]
    js = [main_cluster_last_join(r) for r in slp]
    add(sec, "last member join", "per run", "FIX", "SLP", welch(jf, js), unit="s")
    rf = [formation_from_reset(r).get("join_retries_total") for r in fix]
    rs = [formation_from_reset(r).get("join_retries_total") for r in slp]
    add(sec, "join retries", "per run", "FIX", "SLP", welch(rf, rs))
    lf = [_p95(app_latencies_ms(r)) for r in fix]
    ls = [_p95(app_latencies_ms(r)) for r in slp]
    add(sec, "app latency p95", "per run", "FIX", "SLP", welch(lf, ls), unit="ms")
    mf = [x for r in fix for x in app_latencies_ms(r)]
    ms = [x for r in slp for x in app_latencies_ms(r)]
    add(sec, "app latency", "per message (clustered)", "FIX", "SLP", welch(mf, ms), unit="ms")
    nf = _mean_by_node([per_node_pdr(r) for r in fix])
    ns = _mean_by_node([per_node_pdr(r) for r in slp])
    add_paired(sec, "PDR per sender", "FIX", "SLP", paired(nf, ns, 0.02), note="TOST margin 0.02")


def _p95(v: list[float]) -> float | None:
    return float(np.percentile(v, 95)) if len(v) >= 2 else None


def _mean_by_node(dicts: list[dict[str, float]]) -> dict[str, float]:
    acc: dict[str, list[float]] = defaultdict(list)
    for d in dicts:
        for k, v in d.items():
            acc[k].append(v)
    return {k: statistics.fmean(v) for k, v in acc.items()}


def stopstart():
    fix = [c for r in runs("lm200s_stopstart__fix", "stopstart_SF9") for c in stopstart_cycles(r)]
    slp = [c for r in runs("lm200s_stopstart__slp", "stopstart_SF9") for c in stopstart_cycles(r)]
    if not fix or not slp:
        return
    sec = "Stop/Start rejoin (S3 FIX vs S4 SLP)"
    a = [c["rejoin_s"] for c in fix if c["rejoin_s"] is not None]
    b = [c["rejoin_s"] for c in slp if c["rejoin_s"] is not None]
    add(sec, "rejoin time", "per cycle", "FIX", "SLP", welch(a, b), tost(a, b, 35.15), unit="s",
        note="TOST margin = 1 superframe (35.15 s)")
    plain = [c["rejoin_s"] for c in slp if c["rejoin_s"] is not None and c["node"] not in WARM_NODES]
    warm = [c["rejoin_s"] for c in slp if c["rejoin_s"] is not None and c["node"] in WARM_NODES]
    add(sec, "rejoin time, plain vs warm restart (SLP)", "per cycle", "plain", "warm", welch(plain, warm),
        unit="s")
    add(sec, "rejoin time, FIX vs SLP plain", "per cycle", "FIX", "SLP plain", welch(a, plain), unit="s")


def formation_ab():
    fix, slp = runs("lm200s_formation__fix", "SF7"), runs("lm200s_formation__slp", "SF7")
    if not fix or not slp:
        return
    sec = "Formation SF7, main cluster (S8 FIX vs S9 SLP)"
    a = [main_cluster_last_join(r) for r in fix]
    b = [main_cluster_last_join(r) for r in slp]
    sf = 24.05
    add(sec, "last member join from reset", "per run", "FIX", "SLP", welch(a, b), tost(a, b, sf), unit="s",
        note="TOST margin = 1 superframe (24.05 s); joins < ~60 s are blurred by the capture gap")
    ra = [formation_from_reset(r).get("join_retries_total") for r in fix]
    rb = [formation_from_reset(r).get("join_retries_total") for r in slp]
    add(sec, "join retries", "per run", "FIX", "SLP", welch(ra, rb))
    ja = [t for r in fix for t in main_cluster_join_times(r)]
    jb = [t for r in slp for t in main_cluster_join_times(r)]
    add(sec, "member join time", "per node (clustered)", "FIX", "SLP", welch(ja, jb), unit="s")


def sleep_cells():
    ctrl = runs("lm200s_sleep__slp", "sleep_control_SF9")
    light = runs("lm200s_sleep__slp", "sleep_light_SF9")
    deep = runs("lm200s_sleep__slp", "sleep_deep_SF9")
    if not ctrl:
        return
    sec = "Sleep (S5 control vs S6 light vs S7 deep), SLP"
    nc = _mean_by_node([per_node_pdr(r) for r in ctrl])
    for lab, rr in (("light", light), ("deep", deep)):
        if not rr:
            continue
        margin = 0.02 if lab == "light" else 0.05
        add_paired(sec, f"PDR per sender, control vs {lab}", "control", lab,
                   paired(nc, _mean_by_node([per_node_pdr(r) for r in rr]), margin),
                   note=f"TOST margin {margin} (sleep gate {lab}_pdr_drop)")
        add(sec, f"app latency, control vs {lab}", "per message (clustered)", "control", lab,
            welch([x for r in ctrl for x in app_latencies_ms(r)], [x for r in rr for x in app_latencies_ms(r)]),
            unit="ms")
    if light and deep:
        add(sec, "app latency, light vs deep", "per message (clustered)", "light", "deep",
            welch([x for r in light for x in app_latencies_ms(r)], [x for r in deep for x in app_latencies_ms(r)]),
            unit="ms")
    for lab, rr in (("light", light), ("deep", deep)):
        if rr:
            add(sec, f"latency from scheduled send, control vs {lab}", "per message (clustered)", "control", lab,
                welch([x for r in ctrl for x in app_latencies_from_schedule_ms(r)],
                      [x for r in rr for x in app_latencies_from_schedule_ms(r)]), unit="ms",
                note="APP_TX -> APP_RX + APP_TX_LATE late_ms (the wait before APP_TX)")


def reliable_slots():
    d2 = runs("lm200s_reliable__fix", "reliable_SF9_dds2")
    d3 = runs("lm200s_reliable__fix", "reliable_SF9_dds3")
    if not d2 or not d3:
        return
    sec = "Reliable SF9, 2 vs 3 data slots (FIX)"
    a = [reliable(r).get("ack_ratio") for r in d2]
    b = [reliable(r).get("ack_ratio") for r in d3]
    add(sec, "ACK ratio inside the run window", "per run", "dds2", "dds3", welch(a, b))
    ua = [reliable(r).get("n_unresolved", 0) / max(reliable(r).get("n_tx", 1), 1) for r in d2]
    ub = [reliable(r).get("n_unresolved", 0) / max(reliable(r).get("n_tx", 1), 1) for r in d3]
    add(sec, "in flight at window end (fraction)", "per run", "dds2", "dds3", welch(ua, ub))
    add(sec, "ACK RTT", "per message (clustered)", "dds2", "dds3",
        welch([x for r in d2 for x in reliable_rtts_ms(r)], [x for r in d3 for x in reliable_rtts_ms(r)]),
        unit="ms")


# ── output ───────────────────────────────────────────────────────────────────

def _paired_verdict(p: dict) -> str:
    if p.get("p") is None:
        return p.get("note", "no test")
    s = "significant (p<0.05)" if p["p"] < 0.05 else "not significant"
    t = p.get("tost")
    if t:
        s += f"; equivalent within ±{fmt(t['margin'])}" if t["equivalent"] else \
             f"; equivalence not shown (±{fmt(t['margin'])}, TOST p={fmt(t['p'])})"
    return s


def to_markdown() -> str:
    out = ["# lm200-sleep campaign — statistical comparison", "",
           "Welch's t-test (two-sided) on the difference **B − A**; 95 % CI of that difference; "
           "Hedges' g; Mann-Whitney U p (n ≥ 3 per arm); TOST equivalence against the gate margin "
           "where one applies. Per-run rows are the primary test. Per-message / per-node / per-cycle "
           "rows have more samples but are clustered within runs, so their p-values are optimistic.", ""]
    section = None
    for r in ROWS:
        if r["section"] != section:
            section = r["section"]
            out += ["", f"## {section}", "",
                    "| Metric | Level | A | B | n A/B | mean A | mean B | B − A | 95 % CI | p | g | MW p | Verdict | Note |",
                    "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        if "paired" in r:
            p = r["paired"]
            out.append(f"| {r['metric']} | {r['level']} | {r['a']} | {r['b']} | {p.get('n_pairs')} pairs | | | "
                       f"{fmt(p.get('mean_diff'))} | {fmt(p.get('ci95'))} | {fmt(p.get('p'))} | | "
                       f"{fmt(p.get('wilcoxon_p'))} (Wilcoxon) | "
                       f"{_paired_verdict(p)} | {r['note']} |")
            continue
        w, eq = r["welch"], r.get("tost")
        u = f" {r['unit']}" if r["unit"] else ""
        out.append(f"| {r['metric']}{u} | {r['level']} | {r['a']} | {r['b']} | {w.get('n_a')}/{w.get('n_b')} | "
                   f"{fmt(w.get('mean_a'))} | {fmt(w.get('mean_b'))} | {fmt(w.get('diff'))} | {fmt(w.get('ci95'))} | "
                   f"{fmt(w.get('p'))} | {fmt(w.get('hedges_g'), 2)} | {fmt(w.get('mw_p'))} | {verdict(w, eq)} | {r['note']} |")
    return "\n".join(out) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out-md", type=Path, default=RUNS / "lm200_sleep_campaign" / "report" / "stats.md")
    ap.add_argument("--out-json", type=Path, default=RUNS / "lm200_sleep_campaign" / "report" / "stats.json")
    args = ap.parse_args()
    ab_13node("SF9_d63000", "SF9", 35.15)
    ab_13node("SF12_d308000", "SF12", 122.1)
    stopstart()
    formation_ab()
    sleep_cells()
    reliable_slots()
    args.out_md.parent.mkdir(parents=True, exist_ok=True)
    args.out_md.write_text(to_markdown())
    args.out_json.write_text(json.dumps(ROWS, indent=1, default=lambda o: list(o) if isinstance(o, tuple) else str(o)))
    print(to_markdown())
    return 0


if __name__ == "__main__":
    sys.exit(main())
