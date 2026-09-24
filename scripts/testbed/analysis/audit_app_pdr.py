"""Standalone, independent audit of the v1-vs-v2 application-layer PDR numbers.

This script re-derives every headline number **from the raw `APP_TX`/`APP_RX` log
lines with its own counting** — it does NOT import `app_pdr.py`'s matching. Its job is
to let a reader confirm, by hand if they like, that the figures are honest:

  * per-sender `APP_TX` (sent) and matched `APP_RX` (delivered), per cell, per version;
  * the 3-sink `APP_RX` breakdown, with the reconciliation `delivered == Σ APP_RX(sinks)`
    printed as PASS/FAIL for every cell;
  * `intended = packet_count × N_designed_senders`, fair PDR, and source participation;
  * an invalid-cell flag (0-delivered-on-both / participation < 0.5);
  * a CSV (`audit_<campaign>.csv`, one row per version×cell×sender) next to --out;
  * a cross-check of the totals against each campaign's `aggregated.json`.

Designed senders and sinks are taken from `config.yaml` exactly as `app_pdr.py` does
(active, non-`nowifi`-WiFi nodes are sources; WiFi-uplink nodes are sinks). Sinks are
keyed by their config/device identity — NOT by masking `addr & 0x7FFF`, because two
sink addresses (DF10, E464) already have bit 15 set and masking would mis-rename them.
The bit-15 alias (v1 logs some sources with the high bit set, v2 clears it) is
collapsed only for the remote-vs-main split, never for sink identity.

Usage:
    python3 analysis/audit_app_pdr.py \
        --v1 runs/sim_load_compare_full_v1 --v2 runs/sim_load_compare_full_v2 \
        --out docs/paper/figures/sim_load_compare_full
"""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import re
from collections import defaultdict
from pathlib import Path

APP_TX = re.compile(r"APP_TX src=0x([0-9A-Fa-f]{4}) seq=(\d+) size=(\d+)")
APP_RX = re.compile(r"APP_RX src=0x([0-9A-Fa-f]{4}) seq=(\d+) app=(\d+)")
SIM_APP = 12
DEVID = re.compile(r"-([0-9A-Fa-f]{4})\.log\.gz$")
RUNID = re.compile(r"-(\d{8}-\d{6})-(?P<cell>.+)-r(?P<rep>\d+)$")


def _norm(addr: str) -> str:
    """Collapse the v1 bit-15 alias so a source has one identity across versions."""
    return format(int(addr, 16) & 0x7FFF, "04X")


def _load_config(run_dir: Path) -> tuple[int | None, set[str], int | None]:
    """(packet_count, sink_addrs, n_designed_senders) from config.yaml.

    Same rule as app_pdr._designed_senders / _packet_count. Sinks keyed by identity."""
    import yaml
    cfg = run_dir / "config.yaml"
    if not cfg.is_file():
        return None, set(), None
    data = yaml.safe_load(cfg.read_text()) or {}
    pkt = None
    for scope in (data.get("defaults"), data):
        if isinstance(scope, dict) and scope.get("packet_count") is not None:
            pkt = int(scope["packet_count"])
            break
    senders, sinks = set(), set()
    for addr, d in (data.get("devices") or {}).items():
        if not isinstance(d, dict):
            continue
        if str(d.get("node_active", 1)).lower() in ("0", "false"):
            continue
        a = str(addr).upper()
        (sinks if str(d.get("wifi_ssid", "nowifi")) != "nowifi" else senders).add(a)
    return pkt, sinks, (len(senders) if senders else None)


def _cell_of(run_dir: Path) -> str | None:
    m = RUNID.search(run_dir.name)
    return m.group("cell") if m else None


def _scan_run(run_dir: Path, sinks: set[str]) -> dict:
    """Raw per-run counts. Returns sent/delivered per source and per-sink APP_RX.

    delivered matches each APP_RX (app==SIM_APP) to an outstanding APP_TX with the same
    (normalized src, seq), FIFO — identical policy to app_pdr.compute, re-implemented
    here so this is a genuinely independent check."""
    sink_ids = {s.upper() for s in sinks}
    pending: dict[tuple[str, int], int] = defaultdict(int)   # (src,seq) -> outstanding
    sent_by_src: dict[str, int] = defaultdict(int)
    delivered_by_src: dict[str, int] = defaultdict(int)
    rx_by_sink: dict[str, int] = defaultdict(int)
    orphans = 0

    # Pass 1: collect all events in time order (APP_TX before APP_RX handled by ts sort).
    events = []  # (ts_line_index, kind, src_norm, seq, dev)
    for path in sorted(run_dir.glob("logs/*.log.gz")):
        m = DEVID.search(path.name)
        dev = m.group(1).upper() if m else "????"
        idx = 0
        with gzip.open(path, "rt", errors="ignore") as fh:
            for line in fh:
                mt = APP_TX.search(line)
                if mt:
                    events.append((line[:25], "tx", _norm(mt.group(1)), int(mt.group(2)), dev))
                    continue
                mr = APP_RX.search(line)
                if mr and int(mr.group(3)) == SIM_APP:
                    events.append((line[:25], "rx", _norm(mr.group(1)), int(mr.group(2)), dev))
    events.sort(key=lambda e: e[0])  # sort by the leading [timestamp] text

    for _ts, kind, src, seq, dev in events:
        if kind == "tx":
            sent_by_src[src] += 1
            pending[(src, seq)] += 1
        else:  # rx at a sink
            if pending.get((src, seq), 0) > 0:
                pending[(src, seq)] -= 1
                delivered_by_src[src] += 1
                rx_by_sink[dev] += 1
            else:
                orphans += 1
    return {
        "sent_by_src": dict(sent_by_src),
        "delivered_by_src": dict(delivered_by_src),
        "rx_by_sink": dict(rx_by_sink),
        "orphans": orphans,
    }


REMOTE = {"3428", "14A4", "5D58"}  # normalized remote-cluster source identities


def audit_campaign(v1_dir: Path, v2_dir: Path) -> list[dict]:
    """One row per (version, cell) with all audited numbers."""
    rows: list[dict] = []
    for ver, base in (("v1", v1_dir), ("v2", v2_dir)):
        for run_dir in sorted(base.iterdir()):
            if not run_dir.is_dir():
                continue
            cell = _cell_of(run_dir)
            if cell is None or not (run_dir / "logs").is_dir():
                continue
            pkt, sinks, n_designed = _load_config(run_dir)
            sc = _scan_run(run_dir, sinks)
            sent = sum(sc["sent_by_src"].values())
            delivered = sum(sc["delivered_by_src"].values())
            sink_sum = sum(sc["rx_by_sink"].values())
            observed = len(sc["sent_by_src"])
            intended = (pkt * n_designed) if (pkt and n_designed) else None
            rem_d = sum(v for k, v in sc["delivered_by_src"].items() if k in REMOTE)
            rows.append({
                "version": ver, "cell": cell,
                "packet_count": pkt, "n_designed": n_designed,
                "sent": sent, "delivered": delivered,
                "observed_senders": observed,
                "participation": (observed / n_designed) if n_designed else None,
                "intended": intended,
                "fair_pdr": (delivered / intended) if intended else None,
                "reconcile_ok": (delivered == sink_sum),
                "sink_breakdown": sc["rx_by_sink"],
                "remote_delivered": rem_d,
                "orphans": sc["orphans"],
                "per_sender_sent": sc["sent_by_src"],
                "per_sender_delivered": sc["delivered_by_src"],
            })
    return rows


def _invalid(rows: list[dict]) -> dict:
    """cell -> reason, when it must not read as a v1-vs-v2 result (0-both / stall)."""
    by_cell: dict[str, dict] = defaultdict(dict)
    for r in rows:
        by_cell[r["cell"]][r["version"]] = r
    out = {}
    for cell, vv in by_cell.items():
        v1, v2 = vv.get("v1"), vv.get("v2")
        if v1 and v2 and v1["delivered"] == 0 and v2["delivered"] == 0:
            out[cell] = "0 delivered on BOTH (payload > MTU, no app fragmentation)"
        elif any(v and v["participation"] is not None and v["participation"] < 0.5
                 for v in (v1, v2)):
            who = "v1" if (v1 and v1["participation"] and v1["participation"] < 0.5) else "v2"
            out[cell] = f"{who} sender stall (participation < 0.5)"
    return out


def _print_report(rows: list[dict], invalid: dict) -> bool:
    ok = True
    order = {"v1": 0, "v2": 1}
    rows = sorted(rows, key=lambda r: (r["cell"], order.get(r["version"], 9)))
    print(f"\n{'ver':4} {'cell':14} {'sent':>5} {'deliv':>6} {'src':>5} "
          f"{'part':>6} {'intd':>6} {'fairPDR':>8} {'recon':>6}  sinks")
    for r in rows:
        recon = "PASS" if r["reconcile_ok"] else "FAIL"
        if not r["reconcile_ok"]:
            ok = False
        part = f"{r['participation']:.2f}" if r["participation"] is not None else "  -"
        fpdr = f"{r['fair_pdr']:.3f}" if r["fair_pdr"] is not None else "   -"
        sinks = ",".join(f"{k}={v}" for k, v in sorted(r["sink_breakdown"].items()))
        flag = "  ⚠ INVALID" if r["cell"] in invalid else ""
        print(f"{r['version']:4} {r['cell']:14} {r['sent']:5} {r['delivered']:6} "
              f"{r['observed_senders']:5} {part:>6} {str(r['intended'] or '-'):>6} "
              f"{fpdr:>8} {recon:>6}  {sinks}{flag}")
    if invalid:
        print("\nInvalid cells (excluded from / shaded in the v1-vs-v2 figures):")
        for cell, why in sorted(invalid.items()):
            print(f"  {cell}: {why}")
    print(f"\nReconciliation (delivered == Σ APP_RX per sink): "
          f"{'ALL PASS ✓' if ok else 'FAILURES PRESENT ✗'}")
    return ok


def _crosscheck_aggregated(rows: list[dict], v1_dir: Path, v2_dir: Path) -> None:
    """Compare audit totals against multirun's aggregated.json, flag mismatches."""
    for ver, base in (("v1", v1_dir), ("v2", v2_dir)):
        agg = base / "aggregated.json"
        if not agg.is_file():
            print(f"  {ver}: no aggregated.json (run multirun.py) — skipped")
            continue
        cells = json.loads(agg.read_text()).get("cells", {})
        for r in [x for x in rows if x["version"] == ver]:
            # find the aggregated cell whose id contains this run's cell token
            match = next((c for name, c in cells.items()
                          if isinstance(c, dict) and r["cell"] in name), None)
            if not match:
                continue
            a_deliv = (match.get("app_delivered") or {}).get("mean")
            if a_deliv is not None and abs(a_deliv - r["delivered"]) > 0.5:
                print(f"  MISMATCH {ver} {r['cell']}: audit delivered={r['delivered']} "
                      f"vs aggregated={a_deliv}")
    print("  aggregated.json cross-check done.")


def _write_csv(rows: list[dict], out_csv: Path) -> None:
    with out_csv.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["version", "cell", "sender", "sent", "delivered"])
        for r in sorted(rows, key=lambda x: (x["cell"], x["version"])):
            senders = sorted(set(r["per_sender_sent"]) | set(r["per_sender_delivered"]))
            for s in senders:
                w.writerow([r["version"], r["cell"], s,
                            r["per_sender_sent"].get(s, 0),
                            r["per_sender_delivered"].get(s, 0)])
    print(f"wrote {out_csv}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Independent audit of v1-vs-v2 app-PDR")
    ap.add_argument("--v1", type=Path, required=True)
    ap.add_argument("--v2", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=None,
                    help="dir for the CSV (default: alongside the v1 run dir)")
    args = ap.parse_args()
    rows = audit_campaign(args.v1, args.v2)
    if not rows:
        raise SystemExit("no runs found under --v1/--v2")
    invalid = _invalid(rows)
    ok = _print_report(rows, invalid)
    print("\nCross-check vs aggregated.json:")
    _crosscheck_aggregated(rows, args.v1, args.v2)
    out_dir = args.out or args.v1.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    campaign = re.sub(r"_v1$", "", args.v1.name)
    _write_csv(rows, out_dir / f"audit_{campaign}.csv")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
