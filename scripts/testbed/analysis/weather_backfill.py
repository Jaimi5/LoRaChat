"""Backfill hourly weather for the testbed campaign window from the Open-Meteo archive.

No weather was logged during the campaign, so we reconstruct it post-hoc. Open-Meteo's
historical archive API is free and needs no key. The testbed is at Campus Nord UPC,
Barcelona (~41.389 N, 2.113 E). Output is cached to JSON so the downstream analysis is
reproducible offline; if there is no internet, supply the same JSON by hand or a CSV with
columns time,temperature_2m,relative_humidity_2m,precipitation,dew_point_2m,wind_speed_10m.

Run from scripts/testbed/:
    python3 analysis/weather_backfill.py 2026-06-05 2026-07-10 --out docs/paper/figures/long_link
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

LAT, LON = 41.389, 2.113  # Campus Nord UPC, Barcelona
TZ = "Europe/Madrid"
HOURLY_VARS = [
    "temperature_2m",
    "relative_humidity_2m",
    "precipitation",
    "dew_point_2m",
    "wind_speed_10m",
]
ARCHIVE_URL = "https://archive-api.open-meteo.com/v1/archive"


def fetch(start: str, end: str) -> dict:
    """Fetch hourly weather for [start, end] (YYYY-MM-DD). Raises on network error."""
    import requests
    params = {
        "latitude": LAT, "longitude": LON,
        "start_date": start, "end_date": end,
        "hourly": ",".join(HOURLY_VARS),
        "timezone": TZ,
    }
    resp = requests.get(ARCHIVE_URL, params=params, timeout=60)
    resp.raise_for_status()
    return resp.json()


def to_rows(payload: dict) -> list[dict]:
    """Flatten Open-Meteo's column-oriented `hourly` block into per-hour rows."""
    hourly = payload.get("hourly") or {}
    times = hourly.get("time") or []
    rows = []
    for i, t in enumerate(times):
        row = {"time": t}
        for v in HOURLY_VARS:
            col = hourly.get(v) or []
            row[v] = col[i] if i < len(col) else None
        rows.append(row)
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("start", help="start date YYYY-MM-DD")
    ap.add_argument("end", help="end date YYYY-MM-DD")
    ap.add_argument("--out", type=Path, default=Path("docs/paper/figures/long_link"))
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    raw_path = args.out / "weather_open_meteo.json"
    csv_path = args.out / "weather_hourly.csv"

    try:
        payload = fetch(args.start, args.end)
    except Exception as e:
        print(f"! weather fetch failed ({e}).")
        print(f"  Provide {csv_path} manually (columns: time,{','.join(HOURLY_VARS)}),")
        print(f"  or {raw_path} in Open-Meteo format, then re-run the plots.")
        return 1

    raw_path.write_text(json.dumps(payload, indent=2))
    rows = to_rows(payload)
    with csv_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["time"] + HOURLY_VARS)
        w.writeheader()
        w.writerows(rows)

    print(f"wrote {len(rows)} hourly rows to {csv_path}")
    print(f"cached raw payload to {raw_path}")
    if rows:
        print(f"range: {rows[0]['time']} .. {rows[-1]['time']} (tz={TZ})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
