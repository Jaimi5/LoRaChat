#!/usr/bin/env python3
"""Render soak_report.py JSON as the daily soak page (static HTML artifact).

Usage:
    python3 soak_page.py soak.json [--baseline baseline.json] -o page.html
"""

from __future__ import annotations

import argparse
import html
import json
import statistics
from pathlib import Path

LIB = "LoRaMesher 2.0.0 (release/2.0.0 41f9b68)"
BASE_LIB = "LoRaMesher 1.0.0 (134ae25), full_deploy_sf10_v3"
TITLE = "LoRaMesher 2.0 Soak"
PLANNED_DAYS = 7

CSS = """
/* Layout: one column of instrument panels; verdict first, then nodes, days, long link, baseline. */
:root {
  --bg: #f5f7f8; --panel: #ffffff; --fg: #17222b; --muted: #5b6b77; --rule: #d9e0e5;
  --accent: #1f6f8b; --accent-soft: #e2eff4;
  --ok: #1d7a46; --ok-bg: #e3f3e9; --warn: #9a6100; --warn-bg: #fbefd9;
  --bad: #b3261e; --bad-bg: #fbe4e1; --na: #6b7780; --na-bg: #eceff1;
  --display: "Barlow Semi Condensed", "Arial Narrow", system-ui, sans-serif;
  --body: "Source Sans 3", system-ui, -apple-system, "Segoe UI", sans-serif;
  --mono: "JetBrains Mono", ui-monospace, "SFMono-Regular", Menlo, Consolas, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #0f1519; --panel: #162026; --fg: #e3eaee; --muted: #93a3ad; --rule: #2a3740;
  --accent: #6cb8d4; --accent-soft: #1b3440;
  --ok: #6fd39a; --ok-bg: #173826; --warn: #f0b860; --warn-bg: #3a2d14;
  --bad: #ff8a80; --bad-bg: #401c1a; --na: #93a3ad; --na-bg: #222d34; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #0f1519; --panel: #162026; --fg: #e3eaee; --muted: #93a3ad; --rule: #2a3740;
  --accent: #6cb8d4; --accent-soft: #1b3440;
  --ok: #6fd39a; --ok-bg: #173826; --warn: #f0b860; --warn-bg: #3a2d14;
  --bad: #ff8a80; --bad-bg: #401c1a; --na: #93a3ad; --na-bg: #222d34; color-scheme: dark }
body { background: var(--bg); color: var(--fg); font: 15px/1.5 var(--body); }
main { max-width: 1120px; margin: 0 auto; display: grid; gap: 28px;
  padding-block: 28px 48px;
  padding-left: max(16px, env(safe-area-inset-left, 0px));
  padding-right: max(16px, env(safe-area-inset-right, 0px)); }
header { display: grid; gap: 10px; }
.eyebrow { font: 600 12px/1 var(--mono); letter-spacing: .08em; text-transform: uppercase; color: var(--accent); }
h1 { font: 600 clamp(28px, 5vw, 40px)/1.1 var(--display); margin: 0; text-wrap: balance; letter-spacing: .01em; }
h2 { font: 600 22px/1.2 var(--display); margin: 0; letter-spacing: .01em; }
p { margin: 0; max-width: 68ch; }
.lede { color: var(--muted); }
.facts { display: flex; flex-wrap: wrap; gap: 6px 18px; font: 13px/1.4 var(--mono); color: var(--muted); }
.facts b { color: var(--fg); font-weight: 600; }
.progress { display: grid; grid-template-columns: repeat(var(--n), 1fr); gap: 4px; max-width: 420px; }
.progress span { height: 8px; border-radius: 2px; background: var(--rule); }
.progress span.done { background: var(--accent); }
.progress span.part { background: linear-gradient(90deg, var(--accent) 50%, var(--rule) 50%); }
section { display: grid; gap: 12px; min-width: 0; }
.panel { background: var(--panel); border: 1px solid var(--rule); border-radius: 6px; min-width: 0; }
.verdict { display: flex; align-items: center; gap: 14px; padding: 14px 16px; flex-wrap: wrap; }
.verdict .big { font: 600 20px/1.2 var(--display); }
.checks { list-style: none; margin: 0; padding: 0; }
.checks li { display: grid; grid-template-columns: 76px 1fr auto; gap: 12px; align-items: baseline;
  padding: 10px 16px; border-top: 1px solid var(--rule); }
.checks li:first-child { border-top: 0; }
.checks .val { font: 600 14px/1.3 var(--mono); text-align: right; font-variant-numeric: tabular-nums; }
.checks .detail { display: block; color: var(--muted); font-size: 13px; }
.pill { display: inline-block; font: 600 11px/1 var(--mono); letter-spacing: .06em; text-transform: uppercase;
  padding: 5px 7px; border-radius: 3px; text-align: center; }
.green { color: var(--ok); background: var(--ok-bg); }
.amber { color: var(--warn); background: var(--warn-bg); }
.red { color: var(--bad); background: var(--bad-bg); }
.info, .n\\/a, .na { color: var(--na); background: var(--na-bg); }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font: 13px/1.35 var(--mono); font-variant-numeric: tabular-nums; }
th, td { padding: 7px 10px; text-align: right; white-space: nowrap; border-top: 1px solid var(--rule); }
th { font: 600 11px/1.2 var(--mono); letter-spacing: .05em; text-transform: uppercase; color: var(--muted);
  border-top: 0; vertical-align: bottom; }
th:first-child, td:first-child { text-align: left; }
td.cl { color: var(--muted); text-align: left; }
tr.sep td { border-top: 2px solid var(--rule); }
td.green, td.amber, td.red, td.na { font-weight: 600; }
.grid td { text-align: center; padding: 6px 4px; min-width: 46px; }
.note { color: var(--muted); font-size: 13px; }
.cols { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 16px; }
.cols > * { min-width: 0; }
code { font: 13px var(--mono); background: var(--accent-soft); padding: 1px 4px; border-radius: 3px; }
footer { color: var(--muted); font-size: 13px; display: grid; gap: 6px; }
@media (max-width: 560px) { .checks li { grid-template-columns: 64px 1fr; } .checks .val { grid-column: 2; text-align: left; } }
"""


def esc(x) -> str:
    return html.escape("" if x is None else str(x))


def fmt(v, p=None, dash="–"):
    if v is None:
        return dash
    if p is not None and isinstance(v, (int, float)):
        return f"{v:.{p}f}"
    return str(v)


def cls_pct(v, green, amber):
    if v is None:
        return "na"
    return "green" if v >= green else "amber" if v >= amber else "red"


def cls_zero(v):
    if v is None:
        return "na"
    return "green" if v == 0 else "red"


def overall(checks):
    gated = [c["status"] for c in checks if c["status"] not in ("info", "n/a")]
    if "red" in gated:
        return "red", "Attention needed"
    if "amber" in gated:
        return "amber", "Running, with warnings"
    return "green", "All gated checks pass"


def node_table(rep):
    rows = []
    for cluster in ("main", "remote"):
        first = True
        for k, r in rep["nodes"].items():
            if r["cluster"] != cluster:
                continue
            crash = r["crash_boots"] + r["crash_markers"]
            gated = cluster == "main"
            jcls = cls_pct(r["joined_pct"], 99, 95) if gated else "na"
            bcls = cls_pct(r["beacon_cov_pct"], 95, 80) if gated else "na"
            role = " NM" if k == rep["manager"] else " MQTT" if k == rep["uplink"] else ""
            rows.append(
                f'<tr class="{"sep" if first and cluster == "remote" else ""}">'
                f"<td>{esc(k)}<span class='note'>{esc(role)}</span></td><td class='cl'>{cluster}</td>"
                f"<td class='{cls_zero(r['reboots']) if gated else ''}'>{r['reboots']}</td>"
                f"<td class='{cls_zero(crash)}'>{crash}</td>"
                f"<td class='{cls_zero(r['fault_recoveries']) if gated else ''}'>{r['fault_recoveries']}</td>"
                f"<td>{r['rejoins']}</td>"
                f"<td class='{jcls}'>{fmt(r['joined_pct'], 2)}</td>"
                f"<td class='{bcls}'>{fmt(r['beacon_cov_pct'], 1)}</td>"
                f"<td>{fmt(r['heap_last_day_min'])}</td><td>{fmt(r['heap_slope_b_per_day'])}</td>"
                f"<td>{fmt(r['stack_min_free'])}</td>"
                f"<td>{r['mon_tx_uplink']}</td><td>{fmt(r['mon_delivery_pct'], 1)}</td>"
                f"<td>{r['gaps']}</td></tr>")
            first = False
    head = ("<tr><th>Node</th><th>Cluster</th><th>Re&shy;boots</th><th>Crash</th><th>Fault<br>recov.</th>"
            "<th>Re&shy;joins</th><th>Joined<br>%</th><th>Beacon<br>cov. %</th><th>Heap min<br>last day, B</th>"
            "<th>Heap<br>B/day</th><th>Stack<br>min, B</th><th>Mon msgs<br>sent</th><th>Deliv.<br>%</th>"
            "<th>Log<br>gaps</th></tr>")
    return f"<div class='scroll'><table>{head}{''.join(rows)}</table></div>"


def day_grid(rep, key, green, amber, title):
    days = rep["days"]
    head = "<tr><th>Node</th>" + "".join(f"<th>{esc(d[5:])}</th>" for d in days) + "</tr>"
    rows = []
    for k, r in rep["nodes"].items():
        cells = []
        for d in days:
            v = rep["per_day"].get(k, {}).get(d, {}).get(key)
            if key == "beacon_cov" and v is not None:
                v = 100 * v
            c = cls_pct(v, green, amber) if r["cluster"] == "main" else "na"
            cells.append(f"<td class='{c if v is not None else ''}'>{fmt(v, 1)}</td>")
        rows.append(f"<tr><td>{esc(k)}</td>{''.join(cells)}</tr>")
    return (f"<div class='panel'><div style='padding:12px 16px 0'><h3 class='eyebrow' style='margin:0'>"
            f"{esc(title)}</h3></div><div class='scroll'><table class='grid'>{head}{''.join(rows)}"
            f"</table></div></div>")


def events_by_day(rep):
    days = rep["days"]
    keys = [("boots", "Reboots"), ("fault_recoveries", "Fault recoveries"), ("joins", "Joins"),
            ("gaps", "Log gaps"), ("mon_restarts", "Monitor restarts"), ("warn_lines", "WARN/ERROR lines")]
    head = "<tr><th>Fleet, per day</th>" + "".join(f"<th>{esc(d[5:])}</th>" for d in days) + "</tr>"
    rows = []
    for key, label in keys:
        vals = [sum((rep["per_day"].get(k, {}).get(d, {}).get(key) or 0) for k in rep["nodes"]) for d in days]
        rows.append(f"<tr><td>{label}</td>" + "".join(f"<td>{v}</td>" for v in vals) + "</tr>")
    return f"<div class='panel scroll'><table>{head}{''.join(rows)}</table></div>"


def longlink_table(rep):
    out = []
    for edge, days in rep["longlink"].items():
        rows = []
        for d, v in days.items():
            cov = v.get("ll_beacon_cov")
            rows.append(f"<tr><td>{esc(d)}</td><td>{fmt(None if cov is None else 100 * cov, 1)}</td>"
                        f"<td>{v['ll_rtables']}</td><td>{v['ll_rx']}</td>"
                        f"<td>{fmt(v['ll_rssi_med'])}</td><td>{fmt(v['ll_rssi_p10'])}</td>"
                        f"<td>{fmt(v['ll_snr_med'])}</td><td>{fmt(v['ll_snr_p10'])}</td></tr>")
        a, b = edge.split("->")
        out.append(
            f"<div class='panel'><div style='padding:12px 16px 0'><h3 class='eyebrow' style='margin:0'>"
            f"Heard at {esc(b)} from {esc(a)}</h3></div><div class='scroll'><table>"
            "<tr><th>Day</th><th>Superframes<br>with beacon %</th><th>Routing<br>tables</th><th>All<br>packets</th>"
            "<th>RSSI med<br>dBm</th><th>RSSI p10<br>dBm</th><th>SNR med<br>dB</th><th>SNR p10<br>dB</th></tr>"
            f"{''.join(rows)}</table></div></div>")
    return "<div class='cols'>" + "".join(out) + "</div>"


def fleet_numbers(rep):
    """Headline numbers comparable between this run and the baseline."""
    main = [r for r in rep["nodes"].values() if r["cluster"] == "main"]
    remote = [r for r in rep["nodes"].values() if r["cluster"] == "remote"]
    ndays = max(1, len(rep["days"]))

    def med(xs):
        xs = [x for x in xs if x is not None]
        return round(statistics.median(xs), 2) if xs else None

    def mn(xs):
        xs = [x for x in xs if x is not None]
        return min(xs) if xs else None

    ll = list(rep["longlink"].values())
    ll_days = [v for days in ll for v in days.values()]
    rssi = [v["ll_rssi_med"] for v in ll_days if v["ll_rssi_med"] is not None]
    snr = [v["ll_snr_med"] for v in ll_days if v["ll_snr_med"] is not None]
    bcn = [v["ll_beacon_cov"] for v in ll_days if v.get("ll_beacon_cov") is not None]
    return {
        "Nodes logged": len(rep["nodes"]),
        "Days of data": len(rep["days"]),
        "Superframe, s": rep.get("superframe_s"),
        "Main: reboots": sum(r["reboots"] for r in main),
        "Main: crash resets": sum(r["crash_boots"] + r["crash_markers"] for r in main),
        "Main: fault recoveries": sum(r["fault_recoveries"] for r in main),
        "Main: time joined, worst node %": mn([r["joined_pct"] for r in main]),
        "Main: beacon coverage, median node %": med([r["beacon_cov_pct"] for r in main]),
        "Remote: rejoins per day": round(sum(r["rejoins"] for r in remote) / ndays, 2),
        "Remote: time joined, worst node %": mn([r["joined_pct"] for r in remote]),
        "Long link: beacon reception, median day %": round(100 * statistics.median(bcn), 1) if bcn else None,
        "Long link: RSSI, median of daily medians, dBm": med(rssi),
        "Long link: SNR, median of daily medians, dB": med(snr),
        "Task stack min free, B": mn([r["stack_min_free"] for r in rep["nodes"].values()]),
        "WARN/ERROR lines per node-day": round(sum(r["warn_lines"] for r in rep["nodes"].values())
                                               / max(1, len(rep["nodes"]) * ndays), 1),
    }


def baseline_table(rep, base):
    a, b = fleet_numbers(rep), fleet_numbers(base)
    rows = "".join(f"<tr><td>{esc(k)}</td><td>{fmt(a[k])}</td><td>{fmt(b[k])}</td></tr>" for k in a)
    return (f"<div class='panel scroll'><table><tr><th>Metric</th><th>2.0.0 (this run)</th>"
            f"<th>1.0.0 (Sep 28 – Oct 5)</th></tr>{rows}</table></div>")


def warnings_list(rep, n=12):
    if not rep["warnings"]:
        return "<p class='note'>No WARNING or ERROR lines.</p>"
    first_day = rep["days"][0] if rep["days"] else ""
    rows = "".join(
        f"<tr><td style='text-align:right'>{w['count']}</td><td>{esc(w['first_day'][5:])}"
        f"{' new' if w['first_day'] != first_day else ''}</td><td>{len(w['nodes'])}</td>"
        f"<td style='text-align:left;white-space:normal;min-width:280px'>{esc(w['template'])}</td></tr>"
        for w in rep["warnings"][:n])
    return (f"<div class='panel scroll'><table><tr><th style='text-align:right'>Lines</th><th>First seen</th>"
            f"<th>Nodes</th><th style='text-align:left'>Template (numbers → N)</th></tr>{rows}</table></div>")


def render(rep, base):
    status, verdict = overall(rep["checks"])
    ndays = len(rep["days"])
    bars = "".join(f"<span class='{'done' if i < ndays - 1 else 'part' if i == ndays - 1 else ''}'></span>"
                   for i in range(PLANNED_DAYS))
    checks = "".join(
        f"<li><span class='pill {esc(c['status']).replace('/', '')}'>{esc(c['status'])}</span>"
        f"<span>{esc(c['check'])}<span class='detail'>{esc(c['detail'])}</span></span>"
        f"<span class='val'>{esc(fmt(c['value']))}</span></li>" for c in rep["checks"])
    cfg = next((r["cfg"] for r in rep["nodes"].values() if r.get("cfg")), None)
    base_html = ""
    if base:
        base_html = f"""
<section>
  <h2>Against the old library</h2>
  <p class="lede">The same deployment (same nodes, SF10, 006C as manager, monitor every 900 s) ran for a week
  on {esc(BASE_LIB)}. Its logs were taken at DEBUG and miss the uplink node and four others, so delivery and
  heap figures exist only for this run.</p>
  {baseline_table(rep, base)}
</section>"""
    return f"""<title>{TITLE}</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@600&family=JetBrains+Mono:wght@400;600&family=Source+Sans+3:wght@400;600&display=swap">
<style>{CSS}</style>
<main>
<header>
  <span class="eyebrow">Testbed soak · Campus Nord UPC · 16 nodes</span>
  <h1>{TITLE}</h1>
  <p class="lede">The default LoRaChat firmware runs on {esc(LIB)} for {PLANNED_DAYS} days at SF10. Nodes send
  their routing tables to the MQTT uplink every 15 minutes. This page is rebuilt from the gateway logs, one finished
  day at a time.</p>
  <div class="facts">
    <span>Data <b>{esc(rep['data_from'])}</b> → <b>{esc(rep['data_up_to'])}</b> (gateway time)</span>
    <span>Day <b>{ndays}</b> of {PLANNED_DAYS}</span>
    <span>Manager <b>0x{esc(rep['manager'])}</b></span>
    <span>Uplink <b>0x{esc(rep['uplink'])}</b></span>
    <span>Superframe <b>{fmt(rep.get('superframe_s'))} s</b></span>
  </div>
  <div class="progress" style="--n:{PLANNED_DAYS}" aria-label="Day {ndays} of {PLANNED_DAYS}">{bars}</div>
</header>

<section>
  <h2>Status</h2>
  <div class="panel">
    <div class="verdict"><span class="pill {status}">{status}</span><span class="big">{verdict}</span></div>
    <ul class="checks">{checks}</ul>
  </div>
  <p class="note">Gated checks cover the 13-node main cluster. The remote cluster (3428, DD58, 14A4) hangs on one
  3.5 km link whose quality depends on weather and interference, so it is reported but not gated.</p>
</section>

<section>
  <h2>Nodes</h2>
  <div class="panel">{node_table(rep)}</div>
  <p class="note">Reboots exclude the boot from the firmware upload. Joined % is the share of logged time in the
  normal-operation or manager state. Beacon coverage is the share of superframes, counted from the manager's own
  sync beacons, in which a node heard at least one beacon, direct or forwarded. Delivery compares the monitor
  messages a node sent to the uplink with those that reached it. Log gaps are stretches of more than
  {int(rep['gap_threshold_s'] // 60)} minutes with no serial output, which points at the gateway or USB rather than
  the node.</p>
</section>

<section>
  <h2>Day by day</h2>
  {events_by_day(rep)}
  <div class="cols">
    {day_grid(rep, 'joined_pct', 99, 95, 'Time joined, %')}
    {day_grid(rep, 'beacon_cov', 95, 80, 'Sync-beacon coverage, %')}
  </div>
</section>

<section>
  <h2>Long link 006C ↔ 3428</h2>
  <p class="lede">Both ends transmit at 20 dBm; every other node uses 2 dBm. SF10 needs an SNR above about −15 dB to
  demodulate.</p>
  {longlink_table(rep)}
</section>
{base_html}
<section>
  <h2>Warnings and errors</h2>
  {warnings_list(rep)}
</section>

<footer>
  <span>Generated {esc(rep['generated'])} from <code>soak_report.py</code> (summary v{rep['summary_version']}).
  {esc(cfg) if cfg else ''}</span>
  <span>Gateways run the monitors on their own, restart them after USB drops or reboots, and cut the logs into
  daily files at 00:05. The page lags until the next update.</span>
</footer>
</main>
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("report", type=Path)
    ap.add_argument("--baseline", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True)
    a = ap.parse_args()
    rep = json.loads(a.report.read_text())
    base = json.loads(a.baseline.read_text()) if a.baseline and a.baseline.exists() else None
    a.out.write_text(render(rep, base))
    print(f"wrote {a.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
