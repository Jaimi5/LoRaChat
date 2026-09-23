"""Cross-experiment RSSI/SNR/reception corpus for the 3.5 km long link (006C<->3428).

Walks every run under a `runs/` tree, extracts every reception on the long link, and
writes a tidy long-form CSV plus per-hour reception-rate and linkstats tables. The goal
is to characterize the link's health over wall-clock time (diurnal + day-to-day) across
the whole campaign, so it can be overlaid against a weather archive (weather_backfill.py)
and plotted (plot_link_health.py).

The link is the pair {006C, 3428}. A reception is "long-link" when the receiver (the node
whose log it is) and the sender are exactly that pair.

METHODOLOGICAL CAVEATS baked into the output (see the paper findings doc):
  1. Survivorship bias — RSSI/SNR exist only for *successful* decodes. Deep fades show up
     as FEWER samples (lower per-hour reception rate), not lower RSSI. Treat reception rate
     / linkstats exp-vs-recv as the primary "health" signal; RSSI/SNR are secondary.
  2. v1's PHY RSSI/SNR line ("Receiving LoRa packet: ... RSSI ... SNR") is address-blind,
     but the very next line ("Packet received -- ... Src ... Dst ...") carries the decoded
     header. We pair the two adjacent lines to attribute each v1 reception to its true
     source (confidence="v1_paired") — no RSSI heuristic. Unpaired RSSI lines (CRC-failed
     receives with no header) are dropped.
  3. Samples pool across SF7/9/12 and 2/14 dBm — every row is tagged with its run's SF and
     the pair's TX power so downstream code can normalize/facet per SF.

Run from scripts/testbed/:
    python3 analysis/link_health_corpus.py runs --out docs/paper/figures/long_link
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

if __name__ == "__main__":
    import sys
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import Event, is_v2_run, parse_run, v2_runtime_addr

# The two endpoints of the long link (config/v1 labels; both are < 0x8000 so their v2
# runtime address is identical — no remap needed).
PAIR = ("006C", "3428")

# Max gap between a v1 "Receiving LoRa packet" (RSSI/SNR) line and the following
# "Packet received" (Src/Dst) header line for them to be treated as the same packet.
# They are emitted back-to-back (~ms apart); this guard just prevents pairing across a
# CRC-failed receive that produced no header.
V1_PAIR_WINDOW_S = 2.0


def _v1_rssi_true(r: int) -> float:
    """Recover the true RSSI (dBm) from v1's int8_t-truncated log value.

    v1 firmware does `rssi = (int8_t)round(radio->getRSSI())` (LoraMesher.cpp:404), so any
    RSSI below −128 dBm OVERFLOWS the signed 8-bit range and prints as a positive number:
    (int8_t)(−131) == +125. RSSI is physically always negative, so a non-negative logged
    value is an overflow and the true value is `v − 256` (verified in the logs: v1 RSSI is
    −70…−128 then jumps straight to +121…+125, i.e. −135…−131, with a clean gap between).
    SNR is also int8_t but its range (~−20…+13 dB) never overflows, so it needs no fix.
    """
    return float(r) if r < 0 else float(r) - 256.0

# Diurnal "hour of day" is computed in local time: RF/thermal/human-activity cycles follow
# local solar time, not UTC. All campaign data is CEST (UTC+2), but use the zone for safety.
LOCAL_TZ = ZoneInfo("Europe/Madrid")


def _read_config(run_dir: Path) -> dict:
    cfg = run_dir / "config.yaml"
    if not cfg.is_file():
        return {}
    try:
        import yaml
        return yaml.safe_load(cfg.read_text()) or {}
    except Exception:
        return {}


def _run_sf(cfg: dict) -> int | None:
    for scope in (cfg.get("defaults"), cfg):
        if isinstance(scope, dict) and scope.get("lora_spreading_factor") is not None:
            try:
                return int(scope["lora_spreading_factor"])
            except (TypeError, ValueError):
                return None
    return None


def _pair_tx_power(cfg: dict) -> int | None:
    """TX power of the long-link endpoints (they are configured identically, 14 dBm)."""
    devices = cfg.get("devices")
    if not isinstance(devices, dict):
        return None
    for addr, d in devices.items():
        if str(addr).upper() in PAIR and isinstance(d, dict) and d.get("lora_power") is not None:
            try:
                return int(d["lora_power"])
            except (TypeError, ValueError):
                return None
    return None


def _other_endpoint(node: str) -> str:
    return PAIR[1] if node.upper() == PAIR[0] else PAIR[0]


def _local_hour(ts: float) -> int:
    return datetime.fromtimestamp(ts, tz=LOCAL_TZ).hour


def _local_date(ts: float) -> str:
    return datetime.fromtimestamp(ts, tz=LOCAL_TZ).strftime("%Y-%m-%d")


def extract_run(run_dir: Path) -> tuple[list[dict], list[dict]]:
    """Return (rx_rows, linkstats_rows) for one run's long-link receptions.

    Each rx row is one successful reception on the pair. linkstats rows are the
    survivorship-unbiased exp/recv/missed counters for the pair (v2 only, LOG_DEBUG).
    """
    events = parse_run(run_dir)  # default = measurement window from lifecycle.json
    if not events:
        return [], []

    cfg = _read_config(run_dir)
    sf = _run_sf(cfg)
    tx = _pair_tx_power(cfg)
    version = "v2" if is_v2_run(events) else "v1"
    batch = run_dir.parent.name
    run_id = run_dir.name

    rx_rows: list[dict] = []
    ls_rows: list[dict] = []
    # v1 attribution: the RSSI/SNR line ("Receiving LoRa packet") is address-blind; the
    # decoded header with the real Src/Dst is on the very next line ("Packet received").
    # Hold each node's most-recent RSSI/SNR line and pair it with that node's next header
    # line if it arrives within V1_PAIR_WINDOW_S (they are ~ms apart). Unpaired RSSI lines
    # are CRC-failed receives we cannot attribute, so we drop them.
    v1_pending: dict[str, tuple[float, int, int]] = {}  # node -> (ts, rssi, snr)

    for ev in events:
        node = ev.node.upper()
        if node not in PAIR:
            continue

        if ev.kind == "pkt_rx":  # v2: src-tagged, high confidence
            src = str(ev.fields.get("src", "")).upper()
            if src != _other_endpoint(node):
                continue
            rx_rows.append(_rx_row(ev, node, src, ev.fields["rssi"], ev.fields["snr"],
                                   "v2_pkt_rx", sf, tx, version, batch, run_id))

        elif ev.kind == "v1_pkt_recv":  # v1: RSSI/SNR now; source comes on the next line
            v1_pending[node] = (ev.ts, ev.fields["rssi"], ev.fields["snr"])

        elif ev.kind == "v1_pkt_received":  # v1: the header with the REAL Src/Dst
            pend = v1_pending.pop(node, None)
            if pend is None or ev.ts - pend[0] > V1_PAIR_WINDOW_S:
                continue  # no matching RSSI line for this header
            src = str(ev.fields.get("src", "")).upper()
            if src != _other_endpoint(node):
                continue  # correctly attributed to a non-long-link sender — skip
            _ts, rssi, snr = pend
            # Recover the true RSSI from v1's int8_t overflow (below −128 dBm wraps to a
            # positive print). NOTE: the old rssi<−110 heuristic wrongly EXCLUDED these
            # long-link packets because they print POSITIVE; header-pairing finds them by
            # their true source and _v1_rssi_true un-wraps the value.
            rx_rows.append(_rx_row(ev, node, src, _v1_rssi_true(rssi), float(snr),
                                   "v1_paired", sf, tx, version, batch, run_id))

        elif ev.kind == "linkstats":  # v2 exp/recv/missed for the pair
            if str(ev.fields.get("peer", "")).upper() != v2_runtime_addr(_other_endpoint(node)):
                continue
            ls_rows.append({
                "ts_utc": round(ev.ts, 3),
                "date": _local_date(ev.ts),
                "hour_of_day": _local_hour(ev.ts),
                "receiver": node,
                "peer": _other_endpoint(node),
                "exp": ev.fields["exp"],
                "recv": ev.fields["recv"],
                "missed": ev.fields["missed"],
                "ewma": ev.fields["ewma"],
                "quality_to": ev.fields["quality_to"],
                "sf": sf, "tx_power": tx, "version": version,
                "batch": batch, "run_id": run_id,
            })

    return rx_rows, ls_rows


def _rx_row(ev: Event, receiver: str, sender: str, rssi: float, snr: float,
            confidence: str, sf, tx, version, batch, run_id) -> dict:
    return {
        "ts_utc": round(ev.ts, 3),
        "date": _local_date(ev.ts),
        "hour_of_day": _local_hour(ev.ts),
        "direction": f"{sender}->{receiver}",
        "receiver": receiver,
        "sender": sender,
        "rssi": rssi,
        "snr": snr,
        "sf": sf,
        "tx_power": tx,
        "version": version,
        "batch": batch,
        "run_id": run_id,
        "confidence": confidence,
    }


def _hour_bin_rate(rx_rows: list[dict]) -> list[dict]:
    """Per absolute-clock-hour reception count, split by direction+version+SF.

    Reception *rate* (packets/hour) is the survivorship-safe health metric: when the link
    fades, this drops even though surviving packets' RSSI does not.
    """
    counts: dict[tuple, int] = defaultdict(int)
    for r in rx_rows:
        hour_utc = int(r["ts_utc"] // 3600) * 3600
        key = (hour_utc, r["direction"], r["version"], r["sf"], r["batch"])
        counts[key] += 1
    out = []
    for (hour_utc, direction, version, sf, batch), n in sorted(counts.items()):
        out.append({
            "hour_utc": hour_utc,
            "date": _local_date(hour_utc),
            "hour_of_day": _local_hour(hour_utc),
            "direction": direction,
            "version": version,
            "sf": sf,
            "batch": batch,
            "packets": n,
        })
    return out


def _write_csv(rows: list[dict], path: Path, fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)


def build_corpus(runs_root: Path, out_dir: Path) -> dict:
    run_dirs = sorted(p.parent for p in runs_root.glob("*/*/config.yaml"))
    all_rx: list[dict] = []
    all_ls: list[dict] = []
    per_run_summary: list[dict] = []

    for rd in run_dirs:
        try:
            rx, ls = extract_run(rd)
        except Exception as e:  # one bad run must not sink the corpus
            print(f"  ! {rd.parent.name}/{rd.name}: {e}")
            continue
        all_rx.extend(rx)
        all_ls.extend(ls)
        if rx or ls:
            per_run_summary.append({
                "batch": rd.parent.name, "run_id": rd.name,
                "rx_samples": len(rx), "linkstats_rows": len(ls),
            })

    rate_rows = _hour_bin_rate(all_rx)

    _write_csv(all_rx, out_dir / "link_health_corpus.csv", [
        "ts_utc", "date", "hour_of_day", "direction", "receiver", "sender",
        "rssi", "snr", "sf", "tx_power", "version", "batch", "run_id", "confidence",
    ])
    _write_csv(all_ls, out_dir / "link_health_linkstats.csv", [
        "ts_utc", "date", "hour_of_day", "receiver", "peer",
        "exp", "recv", "missed", "ewma", "quality_to",
        "sf", "tx_power", "version", "batch", "run_id",
    ])
    _write_csv(rate_rows, out_dir / "link_health_reception_rate.csv", [
        "hour_utc", "date", "hour_of_day", "direction", "version", "sf", "batch", "packets",
    ])

    dates = sorted({r["date"] for r in all_rx})
    by_version = defaultdict(int)
    by_conf = defaultdict(int)
    for r in all_rx:
        by_version[r["version"]] += 1
        by_conf[r["confidence"]] += 1
    summary = {
        "runs_scanned": len(run_dirs),
        "runs_with_longlink_data": len(per_run_summary),
        "rx_samples_total": len(all_rx),
        "rx_samples_by_version": dict(by_version),
        "rx_samples_by_confidence": dict(by_conf),
        "linkstats_rows_total": len(all_ls),
        "distinct_dates": dates,
        "n_distinct_dates": len(dates),
        "per_run": per_run_summary,
    }
    (out_dir / "link_health_summary.json").write_text(json.dumps(summary, indent=2))
    return summary


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs_root", type=Path, help="runs/ directory (e.g. scripts/testbed/runs)")
    ap.add_argument("--out", type=Path, default=Path("docs/paper/figures/long_link"),
                    help="output directory for the CSVs + summary")
    args = ap.parse_args()

    summary = build_corpus(args.runs_root, args.out)
    print(f"\nscanned {summary['runs_scanned']} runs; "
          f"{summary['runs_with_longlink_data']} carry long-link data")
    print(f"rx samples: {summary['rx_samples_total']} "
          f"({summary['rx_samples_by_version']}, conf={summary['rx_samples_by_confidence']})")
    print(f"linkstats rows: {summary['linkstats_rows_total']}")
    print(f"dates ({summary['n_distinct_dates']}): {', '.join(summary['distinct_dates'])}")
    print(f"wrote CSVs + link_health_summary.json to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
