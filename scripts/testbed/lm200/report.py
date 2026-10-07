#!/usr/bin/env python3
"""Build the LoRaMesher 2.0.0 campaign report from state.json + gates/*.json.

Writes <campaign_dir>/report/{report.md, report.html, verdict.json} and, where
the existing plotting code can handle the lm200 dirs, figures under report/fig/.
Never fails because of a missing piece: absent gates show as NOT RUN and plots
are best-effort.

Verdict:
    NO-GO            preflight/prewarm failed, a gate did not run to completion
                     of T0, or any FAIL check flagged as a blocker
    GO-WITH-CAVEATS  no blocker, but GREY / INCOMPLETE / non-blocking FAIL checks,
                     INCOMPLETE cells, or RadioLib differing between gateways
    GO               every gate PASS, nothing incomplete
"""

from __future__ import annotations

import argparse
import html
import json
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
TESTBED = HERE.parent
sys.path.insert(0, str(TESTBED))
sys.path.insert(0, str(HERE))

import gates  # noqa: E402

GATE_TITLES = {"g_minus1": "G-1 local build", "g0": "G0 smoke (T0)", "g1": "G1 core regression (T1a)",
               "g2": "G2 features & stress (T2)"}


def _load(p: Path):
    try:
        return json.loads(p.read_text())
    except Exception:  # noqa: BLE001
        return None


def _fmt(v, n=80):
    if v is None:
        return ""
    if isinstance(v, float):
        v = round(v, 4)
    s = json.dumps(v, default=list) if isinstance(v, (dict, list)) else str(v)
    return s if len(s) <= n else s[: n - 1] + "…"


def _md_escape(s: str) -> str:
    return str(s).replace("|", "\\|").replace("\n", " ")


def verdict_of(state: dict, gres: dict, forced: str | None, reason: str | None) -> tuple[str, list[str], list[str]]:
    blockers, caveats = [], []
    if forced:
        blockers.append(reason or forced)
    st = state.get("stages", {})
    for name in ("preflight", "prewarm"):
        if st.get(name, {}).get("status") == "failed":
            blockers.append(f"{name} failed")
    if "g0" not in gres:
        blockers.append("G0 smoke gate never evaluated")
    for g, res in gres.items():
        for c in res.get("checks", []):
            label = f"{g}: {c['name']}{' [' + c['run'] + ']' if c.get('run') else ''}"
            if c["status"] == "FAIL":
                (blockers if c.get("blocker", True) else caveats).append(f"{label} — {c.get('detail') or _fmt(c.get('value'))}")
            elif c["status"] in ("GREY", "INCOMPLETE"):
                caveats.append(f"{label} ({c['status']}) — {c.get('detail') or _fmt(c.get('value'))}")
    for name in ("T1a", "T2"):
        if st.get(name, {}).get("status") not in ("done", "skipped") and not forced:
            caveats.append(f"stage {name} did not complete")
    for inc in state.get("incomplete", []):
        caveats.append(f"INCOMPLETE cell {inc['batch']}/{inc['cell']}")
    # RadioLib is pinned in platformio.ini; the compiled version is checked per run
    # from the upload dependency graph (radiolib gate check). The libdeps probe only
    # sees LoRaMesher's own unpinned ^7.1.2 copy, which is not compiled.
    if blockers:
        return "NO-GO", blockers, caveats
    return ("GO-WITH-CAVEATS" if caveats else "GO"), blockers, caveats


def _triage(campaign_dir: Path, gres: dict) -> list[str]:
    """First crash lines / missing nodes for failing runs (from cached summaries)."""
    out = []
    for g, res in gres.items():
        for c in res.get("checks", []):
            if c["status"] == "FAIL" and c["name"] in ("crashes", "boot", "reboots", "flash", "toa_ldro",
                                                        "join", "lib_sha") and c.get("detail"):
                out.append(f"- **{g} {c['name']}** {c.get('run', '')}: {c['detail'][:500]}")
    return out


def _plots(campaign_dir: Path, state: dict) -> list[str]:
    """Best-effort figures with the existing tooling; never raises."""
    figs = []
    fig = campaign_dir / "report" / "fig"
    fig.mkdir(parents=True, exist_ok=True)
    pairs = [("lm200_13node", "runs/sim_load_compare_13node__lmv2", 13, 3),
             ("lm200_reach16", "runs/sim_load_compare_reach16__lmv2", 16, 5)]
    for name, base, nodes, hops in pairs:
        new = TESTBED / "runs" / name
        if not new.is_dir() or not gates.run_dirs(new):
            continue
        try:
            # plot_compare reads <dir>/aggregated.json. The lm200 batch dir is ours to
            # write; the July baseline already carries its own aggregated.json.
            subprocess.run([sys.executable, str(TESTBED / "analysis" / "multirun.py"), str(new)],
                           cwd=TESTBED, check=True, capture_output=True, timeout=3600)
            out = fig / name
            r = subprocess.run([sys.executable, str(TESTBED / "analysis" / "plot_compare.py"),
                                "--v1", str(TESTBED / base), "--v2", str(new), "--out", str(out),
                                "--nodes", str(nodes), "--max-hops", str(hops),
                                "--v1-label", "LoRaMesher 134ae25 (July)",
                                "--v2-label", "LoRaMesher 2.0.0"],
                               cwd=TESTBED, capture_output=True, text=True, timeout=1800)
            if r.returncode == 0:
                figs += [str(p.relative_to(campaign_dir / "report")) for p in sorted(out.glob("*.png"))]
            else:
                figs.append(f"(plot_compare failed for {name}: {r.stderr.strip()[-300:]})")
        except Exception as e:  # noqa: BLE001
            figs.append(f"(figures for {name} skipped: {e})")
    ab = state.get("ab")
    if ab:
        figdir = campaign_dir / "figures_ab"
        figs += [str(p.relative_to(campaign_dir)) for p in sorted(figdir.rglob("*.png"))] if figdir.is_dir() else []
    return figs


def build(campaign_dir: Path, cfg: dict, forced: str | None, reason: str | None, plots: bool) -> dict:
    state = _load(campaign_dir / "state.json") or {}
    gres = {}
    for g in ("g_minus1", "g0", "g1", "g2"):
        r = _load(campaign_dir / "gates" / f"{g}.json")
        if r:
            gres[g] = r
    verdict, blockers, caveats = verdict_of(state, gres, forced, reason)
    rep = campaign_dir / "report"
    rep.mkdir(parents=True, exist_ok=True)
    figs = _plots(campaign_dir, state) if plots else []
    headline = (f"{len(blockers)} blocker(s), {len(caveats)} caveat(s)" if verdict != "GO"
                else "all gates PASS")

    md = [f"# LoRaMesher 2.0.0 testbed campaign — **{verdict}**", "",
          f"Generated {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')}. "
          f"Library `{gates.dig(cfg, 'lib_sha.new')}` (release/2.0.0) vs July baseline "
          f"`{gates.dig(cfg, 'lib_sha.old')}`; LoRaChat `{(state.get('lorachat_head') or '?')[:9]}`. "
          f"Campaign status: {state.get('status', '?')}.", "",
          f"**{headline}.**", ""]
    if blockers:
        md += ["## Blockers", ""] + [f"- {_md_escape(b)}" for b in blockers] + [""]
    md += ["## Stages", "", "| Stage | Status | Gate | Time (UTC) |", "|---|---|---|---|"]
    for name in ("preflight", "prewarm", "T0", "T1a", "T1b", "T2", "report"):
        s = state.get("stages", {}).get(name)
        if s:
            md.append(f"| {name} | {s.get('status')} | {s.get('gate', '') or ''} | {s.get('ts', '')} |")
    md.append("")
    rl = state.get("radiolib_prewarm")
    rlh = state.get("radiolib") or {}
    if rlh:
        md += ["## Unpinned RadioLib copy per gateway (not compiled)", "",
               "Probed from each gateway's `.pio/libdeps/<env>/RadioLib`, which holds LoRaMesher's own "
               "`^7.1.2` dependency. The firmware links the pinned `RadioLib@<version>` copy instead; "
               "the compiled version is the `radiolib` check in each gate below, read from the upload "
               "dependency graph. Recorded only, never changed.", "",
               "| When | Versions per gateway |", "|---|---|"]
        md += [f"| {k} | `{_md_escape(json.dumps(v))}` |" for k, v in rlh.items()] + [""]
    for g, res in gres.items():
        counts = Counter(c["status"] for c in res["checks"])
        md += [f"## {GATE_TITLES.get(g, g)} — {res['status']}", "",
               " · ".join(f"{k} {v}" for k, v in sorted(counts.items())), ""]
        if g == "g1" and res.get("ab_trigger"):
            md += ["_G1 had GREY regression checks → interleaved A/B (T1b) was triggered._", ""]
        md += ["| Status | Check | Run/cell | Value | Threshold | Detail |", "|---|---|---|---|---|---|"]
        order = {"FAIL": 0, "INCOMPLETE": 1, "GREY": 2, "PASS": 3}
        rows = sorted(res["checks"], key=lambda c: order.get(c["status"], 9))
        # Collapse repetitive PASS rows (one per run) into a count per check name.
        pass_counts = Counter(c["name"] for c in rows if c["status"] == "PASS")
        shown_pass = set()
        for c in rows:
            if c["status"] == "PASS" and pass_counts[c["name"]] > 2:
                if c["name"] in shown_pass:
                    continue
                shown_pass.add(c["name"])
                md.append(f"| PASS | {_md_escape(c['name'])} | ×{pass_counts[c['name']]} runs | | | |")
                continue
            md.append("| {} | {} | {} | {} | {} | {} |".format(
                c["status"], _md_escape(c["name"]), _md_escape(c.get("run") or c.get("cell") or ""),
                _md_escape(_fmt(c.get("value"))), _md_escape(_fmt(c.get("threshold"))),
                _md_escape(_fmt(c.get("detail"), 200))))
        md.append("")
        if g == "g1" and res.get("baseline"):
            md += ["<details><summary>July baseline values used</summary>", "", "```json",
                   json.dumps(res["baseline"], indent=1, default=list)[:6000], "```", "</details>", ""]
    if caveats:
        md += ["## Caveats", ""] + [f"- {_md_escape(c)}" for c in caveats[:200]] + [""]
    inc = state.get("incomplete") or []
    if inc:
        md += ["## Incomplete cells", ""] + [f"- {i['batch']}/{i['cell']} ({i['ts']})" for i in inc] + [""]
    fr = state.get("failed_runs") or []
    if fr:
        md += ["## Runs moved aside (invalid)", ""] + [f"- `{Path(f['run']).name}` — {f['why']}" for f in fr] + [""]
    tri = _triage(campaign_dir, gres)
    if tri:
        md += ["## Triage excerpts", ""] + tri + [""]
    if state.get("ab"):
        md += ["## Interleaved A/B (T1b)", "", f"`{json.dumps(state['ab'])}` — see figures_ab/.", ""]
    if figs:
        md += ["## Figures", ""] + [f"- ![]({f})" if f.endswith(".png") else f"- {f}" for f in figs] + [""]
    md += ["## Known open items in 2.0.0 (context, not gated as blockers)", "",
           "- 4 KB protocol task stack not measured upstream under reliable/group traffic (see G2 stack_free).",
           "- Reliable group ACK completeness ~28% at 25 nodes (upstream TODO-016).",
           "- Receivers keep 32 delivery windows → possible duplicate delivery (G2 group:dup_ratio).",
           "- `echo_ts` unclamped → RTT outliers (excluded and counted in G2 reliable:rtt_p95_ms).",
           "- Data-slot overflow: control indices ≥ max_data_slots get zero slots (behaviour change vs 134ae25).",
           "- `STACK free=` units changed (bytes); never compare with 134ae25 values.", "",
           "## Next steps", "",
           "- GO / GO-WITH-CAVEATS: review caveats, then merge `lm200-eval` into `new_loramesher` and set "
           "`GIT_BRANCH` back in `scripts/testbed/testbed.conf` (not done automatically).",
           "- NO-GO: fix the blockers; re-run with `campaign.py --reset-state` (or `--from-stage T1a` to "
           "keep a passing smoke).", ""]
    (rep / "report.md").write_text("\n".join(md))

    verdict_json = {"verdict": verdict, "headline": headline, "blockers": blockers, "caveats": caveats,
                    "gates": {g: r["status"] for g, r in gres.items()},
                    "stages": {k: v.get("status") for k, v in (state.get("stages") or {}).items()},
                    "incomplete": inc, "radiolib_prewarm": rl,
                    "generated": datetime.now(timezone.utc).isoformat(timespec="seconds")}
    (rep / "verdict.json").write_text(json.dumps(verdict_json, indent=2, default=list))
    (rep / "report.html").write_text(_html(md, verdict))
    return verdict_json


def _html(md_lines: list[str], verdict: str) -> str:
    """Minimal self-contained HTML rendering of the markdown (tables, headings, lists)."""
    color = {"GO": "#1a7f37", "GO-WITH-CAVEATS": "#9a6700", "NO-GO": "#cf222e"}.get(verdict, "#555")
    out, in_table, in_list, in_pre = [], False, False, False
    for line in md_lines:
        if line.startswith("```"):
            out.append("</pre>" if in_pre else "<pre>")
            in_pre = not in_pre
            continue
        if in_pre:
            out.append(html.escape(line))
            continue
        if line.startswith("|"):
            cells = [c.strip().replace("\\|", "|") for c in line.strip("|").split(" | ")]
            if set(line.replace("|", "").strip()) <= {"-"}:
                continue
            if not in_table:
                out.append("<table>")
                in_table = True
                out.append("<tr>" + "".join(f"<th>{html.escape(c)}</th>" for c in cells) + "</tr>")
                continue
            cls = cells[0] if cells and cells[0] in ("PASS", "GREY", "FAIL", "INCOMPLETE") else ""
            out.append(f"<tr class='{cls}'>" + "".join(f"<td>{html.escape(c)}</td>" for c in cells) + "</tr>")
            continue
        if in_table:
            out.append("</table>")
            in_table = False
        if line.startswith("- "):
            if not in_list:
                out.append("<ul>")
                in_list = True
            item = line[2:]
            if item.startswith("![]("):
                src = item[4:-1]
                out.append(f"<li><img src='{html.escape(src)}' style='max-width:100%'></li>")
            else:
                out.append(f"<li>{html.escape(item)}</li>")
            continue
        if in_list:
            out.append("</ul>")
            in_list = False
        if line.startswith("#"):
            lvl = len(line) - len(line.lstrip("#"))
            out.append(f"<h{lvl}>{html.escape(line.lstrip('#').strip())}</h{lvl}>")
        elif line.startswith("<details") or line.startswith("</details"):
            out.append(line)
        elif line.strip():
            out.append(f"<p>{html.escape(line)}</p>")
    if in_table:
        out.append("</table>")
    if in_list:
        out.append("</ul>")
    css = (":root{--bg:#fff;--fg:#1f2328;--line:#d0d7de}"
           "@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--line:#30363d}}"
           "body{background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,sans-serif;max-width:1200px;"
           "margin:0 auto;padding:16px}table{border-collapse:collapse;width:100%;font-size:12px;display:block;"
           "overflow-x:auto}td,th{border:1px solid var(--line);padding:3px 6px;text-align:left;vertical-align:top}"
           "tr.FAIL td:first-child{color:#cf222e;font-weight:700}tr.GREY td:first-child{color:#9a6700}"
           "tr.INCOMPLETE td:first-child{color:#8250df}tr.PASS td:first-child{color:#1a7f37}"
           f"h1{{color:{color}}}pre{{overflow-x:auto;font-size:11px}}")
    return (f"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' "
            f"content='width=device-width,initial-scale=1'><title>LoRaMesher 2.0.0 campaign</title>"
            f"<style>{css}</style></head><body>{''.join(out)}</body></html>")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--campaign-dir", type=Path, default=None)
    ap.add_argument("--config", type=Path, default=None)
    ap.add_argument("--force-status", default=None)
    ap.add_argument("--reason", default=None)
    ap.add_argument("--no-plots", action="store_true")
    a = ap.parse_args(argv)
    cfg = gates.load_config(a.config)
    cdir = a.campaign_dir or gates.tpath(cfg.get("campaign_dir", "runs/lm200_campaign"))
    v = build(cdir, cfg, a.force_status, a.reason, plots=not a.no_plots)
    print(f"verdict: {v['verdict']} — {v['headline']}")
    print(f"report: {cdir / 'report' / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
