"""Parse one run's per-device monitor logs into a normalized event stream.

The regex set mirrors loramesher/log_analyzer.html — every pattern here was
read from that file so analysis stays in sync with what the GUI sees.

A "run directory" has the layout produced by runner.run_batch:
    runs/<batch>/<runid>/
        config.yaml          # frozen per-run experiment config
        lifecycle.json       # phase timestamps incl. measurement-start/end
        logs/                # monitor-dev-<session>-<short_id>.log[.gz]

Each line in a monitor log is prefixed by the gateway wrapper with
`[YYYY-MM-DD HH:MM:SS.mmm] `; everything after that is the original ESP
log line, possibly carrying ANSI color escapes.
"""

from __future__ import annotations

import gzip
import json
import re
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


# ── Regexes mirroring log_analyzer.html ──────────────────────────────────────

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_TS_RE = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]\s*(.*)$")

_PATTERNS = {
    "slot": re.compile(
        r"Slot (\d+) transition: type=(\w+)(?:\s+start=(\d+))?"
    ),
    "pkt_tx": re.compile(
        r"PKT_TX dst=0x([0-9A-Fa-f]+) src=0x([0-9A-Fa-f]+) type=0x([0-9A-Fa-f]+) size=(\d+)"
    ),
    "pkt_rx": re.compile(
        r"PKT_RX src=0x([0-9A-Fa-f]+) dst=0x([0-9A-Fa-f]+) type=0x([0-9A-Fa-f]+) "
        r"size=(\d+) rssi=(-?\d+\.?\d*) snr=(-?\d+\.?\d*)"
    ),
    "data_sent": re.compile(
        r"Sending DATA to 0x([0-9A-Fa-f]+) via 0x([0-9A-Fa-f]+) "
        r"\(ttl=(\d+), seq=(\d+)\), payload_size=(\d+)"
    ),
    "data_delivered": re.compile(
        r"DATA reached final destination: src=0x([0-9A-Fa-f]+), "
        r"dest=0x([0-9A-Fa-f]+), seq=(\d+), payload_size=(\d+)"
    ),
    "data_forwarded": re.compile(
        r"Forwarding DATA: src=0x([0-9A-Fa-f]+), dest=0x([0-9A-Fa-f]+), "
        r"seq=(\d+), ttl=(\d+)"
    ),
    "data_ttl_expired": re.compile(
        r"DATA TTL expired: src=0x([0-9A-Fa-f]+), dest=0x([0-9A-Fa-f]+), "
        r"seq=(\d+), dropping"
    ),
    "data_no_route": re.compile(
        r"No route to dest=0x([0-9A-Fa-f]+) for forwarding: src=0x([0-9A-Fa-f]+), seq=(\d+)"
    ),
    "data_duplicate": re.compile(
        r"Dropping duplicate DATA from 0x([0-9A-Fa-f]+) seq=(\d+)"
    ),
    "rtentry": re.compile(
        r"RTENTRY\s+dest=0x([0-9A-Fa-f]+)\s+via=0x([0-9A-Fa-f]+)\s+"
        r"hops=(\d+)\s+quality=(\d+)\s+active=(\d+)(?:\s+nm=(\d+))?"
    ),
    "linkstats": re.compile(
        r"LinkStats\s+0x([0-9A-Fa-f]+):\s+quality\s+(\d+)\s*->\s*(\d+)\s+"
        r"\(ewma=(\d+)\s+remote=(\d+)\s+exp=(\d+)\s+recv=(\d+)\s+missed=(\d+)\)"
    ),
    "state_change": re.compile(r"Network service state changed to (\d+)"),
    "route_updated": re.compile(
        r"Route updated: dest=0x([0-9A-Fa-f]+) via=0x([0-9A-Fa-f]+) hops=(\d+)"
    ),
    "joined": re.compile(r"Successfully joined network 0x([0-9A-Fa-f]+)"),
}

_SHORT_ID_RE = re.compile(r"monitor-dev-.+?-([0-9A-Fa-f]{4})\.log(?:\.gz)?$")


@dataclass
class Event:
    ts: float            # epoch seconds (UTC)
    node: str            # gateway-derived short id (e.g. "E464")
    kind: str            # one of _PATTERNS keys
    fields: dict = field(default_factory=dict)


def _parse_ts(s: str) -> float:
    # gw-monitor.sh writes naive local time. Treat as UTC for ordering;
    # cross-node latency only matters relatively because sync-time enforces
    # bounded skew across gateways.
    dt = datetime.strptime(s, "%Y-%m-%d %H:%M:%S.%f").replace(tzinfo=timezone.utc)
    return dt.timestamp()


def _strip_ansi(s: str) -> str:
    return _ANSI_RE.sub("", s)


def _open_log(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", errors="replace")
    return path.open("r", errors="replace")


def iter_log_events(path: Path, node: str) -> Iterable[Event]:
    """Yield Events extracted from one device's monitor log file."""
    with _open_log(path) as fh:
        for line in fh:
            m = _TS_RE.match(line)
            if not m:
                continue
            ts_str, rest = m.group(1), _strip_ansi(m.group(2))
            ts = _parse_ts(ts_str)

            for kind, pat in _PATTERNS.items():
                mm = pat.search(rest)
                if not mm:
                    continue
                yield Event(ts=ts, node=node, kind=kind,
                            fields=_extract_fields(kind, mm))
                break  # one event per line


def _extract_fields(kind: str, m: re.Match) -> dict:
    g = m.groups()
    if kind == "slot":
        return {"slot": int(g[0]), "type": g[1],
                "start": int(g[2]) if g[2] else None}
    if kind == "pkt_tx":
        return {"dst": g[0].upper(), "src": g[1].upper(),
                "type": g[2].upper(), "size": int(g[3])}
    if kind == "pkt_rx":
        return {"src": g[0].upper(), "dst": g[1].upper(),
                "type": g[2].upper(), "size": int(g[3]),
                "rssi": float(g[4]), "snr": float(g[5])}
    if kind == "data_sent":
        return {"dst": g[0].upper(), "via": g[1].upper(),
                "ttl": int(g[2]), "seq": int(g[3]),
                "payload_size": int(g[4])}
    if kind == "data_delivered":
        return {"src": g[0].upper(), "dst": g[1].upper(),
                "seq": int(g[2]), "payload_size": int(g[3])}
    if kind == "data_forwarded":
        return {"src": g[0].upper(), "dst": g[1].upper(),
                "seq": int(g[2]), "ttl": int(g[3])}
    if kind == "data_ttl_expired":
        return {"src": g[0].upper(), "dst": g[1].upper(), "seq": int(g[2])}
    if kind == "data_no_route":
        return {"dst": g[0].upper(), "src": g[1].upper(), "seq": int(g[2])}
    if kind == "data_duplicate":
        return {"src": g[0].upper(), "seq": int(g[1])}
    if kind == "rtentry":
        return {"dest": g[0].upper(), "via": g[1].upper(),
                "hops": int(g[2]), "quality": int(g[3]),
                "active": bool(int(g[4])),
                "nm": (bool(int(g[5])) if g[5] else None)}
    if kind == "linkstats":
        return {"peer": g[0].upper(),
                "quality_from": int(g[1]), "quality_to": int(g[2]),
                "ewma": int(g[3]), "remote": int(g[4]),
                "exp": int(g[5]), "recv": int(g[6]), "missed": int(g[7])}
    if kind == "state_change":
        return {"state": int(g[0])}
    if kind == "route_updated":
        return {"dest": g[0].upper(), "via": g[1].upper(), "hops": int(g[2])}
    if kind == "joined":
        return {"network": g[0].upper()}
    return {}


def short_id_from_filename(path: Path) -> str | None:
    m = _SHORT_ID_RE.search(path.name)
    return m.group(1).upper() if m else None


def load_measurement_window(run_dir: Path) -> tuple[float | None, float | None]:
    """Read lifecycle.json and return (start_epoch, end_epoch) for the
    measurement window. Returns (None, None) if the file is missing or
    incomplete — caller can fall back to "all events"."""
    lc = run_dir / "lifecycle.json"
    if not lc.is_file():
        return (None, None)
    phases = json.loads(lc.read_text())
    start = end = None
    for p in phases:
        if p.get("name") == "measurement-start" and p.get("start"):
            start = _iso_to_epoch(p["start"])
        elif p.get("name") == "measurement-end" and p.get("start"):
            end = _iso_to_epoch(p["start"])
    return (start, end)


def _iso_to_epoch(iso: str) -> float:
    # Accept the milliseconds-precision UTC ISO format that RunRecorder writes.
    return datetime.fromisoformat(iso).timestamp()


def parse_run(run_dir: Path,
              window: tuple[float | None, float | None] | None = None,
              ) -> list[Event]:
    """Parse every monitor log in `run_dir/logs/` into one merged event list.

    If `window` is None it is auto-loaded from lifecycle.json. Pass an empty
    window (None, None) explicitly to get the full event stream.
    """
    if window is None:
        window = load_measurement_window(run_dir)
    start, end = window

    logs_dir = run_dir / "logs"
    events: list[Event] = []
    for path in sorted(logs_dir.glob("monitor-dev-*.log*")):
        node = short_id_from_filename(path)
        if not node:
            continue
        for ev in iter_log_events(path, node):
            if start is not None and ev.ts < start:
                continue
            if end is not None and ev.ts > end:
                continue
            events.append(ev)

    events.sort(key=lambda e: e.ts)
    return events


def events_to_jsonl(events: list[Event], out_path: Path) -> int:
    """Persist events to a JSON Lines file. Returns the number of lines."""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w") as f:
        for ev in events:
            f.write(json.dumps(asdict(ev)) + "\n")
    return len(events)


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Parse one run's logs into events.jsonl")
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--all", action="store_true",
                    help="Include events outside the measurement window")
    args = ap.parse_args()

    window = (None, None) if args.all else None
    events = parse_run(args.run_dir, window=window)
    out = args.run_dir / "events.jsonl"
    n = events_to_jsonl(events, out)
    print(f"wrote {n} events to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
