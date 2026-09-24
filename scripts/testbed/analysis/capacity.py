"""Network capacity metrics: offered-load vs PDR, queue saturation signals.

Offered load = total `data_sent` events per minute summed across all active
nodes. PDR is the routing-module's already-computed delivery ratio.
"""

from __future__ import annotations

import json
from pathlib import Path

# Allow direct script invocation from any cwd:
#   python3 scripts/testbed/analysis/capacity.py <run_dir>
if __name__ == "__main__":
    import sys
    from pathlib import Path
    _parent = str(Path(__file__).resolve().parent.parent)
    if _parent not in sys.path:
        sys.path.insert(0, _parent)

from analysis.parse_logs import parse_run
from analysis.routing import compute as compute_routing


def _airtime_ms(sf: int, bw_khz: float, payload_bytes: int,
                cr: int = 5, preamble: int = 8, crc: bool = True,
                low_data_rate_opt: bool | None = None) -> float:
    """LoRa airtime (Semtech AN1200.13). cr is the n in 4/(n+4) i.e. 5..8.

    Returns time-on-air in milliseconds.
    """
    if low_data_rate_opt is None:
        symbol_ms = (2 ** sf) / bw_khz
        low_data_rate_opt = symbol_ms > 16.0
    de = 1 if low_data_rate_opt else 0
    h = 0  # explicit header
    crc_bits = 16 if crc else 0
    payload_symb_nb = 8 + max(
        ((8 * payload_bytes - 4 * sf + 28 + crc_bits - 20 * h) /
         (4 * (sf - 2 * de))) * (cr - 4 + 4),
        0,
    )
    n_sym = preamble + 4.25 + payload_symb_nb
    sym_dur_s = (2 ** sf) / (bw_khz * 1e3)
    return n_sym * sym_dur_s * 1000.0


def analyse(run_dir: Path) -> dict:
    events = parse_run(run_dir)
    if not events:
        return {"empty": True}

    sent_events = [e for e in events if e.kind == "data_sent"]
    if not sent_events:
        return {"offered_pkt_per_min": 0.0, "pdr": None}

    ts_first = sent_events[0].ts
    ts_last = sent_events[-1].ts
    window_min = max((ts_last - ts_first) / 60.0, 1e-6)
    offered_pkt_per_min = len(sent_events) / window_min

    routing = compute_routing(events)
    pdr = routing["totals"]["pdr"]

    # Try to read SF/BW/payload from config.yaml for the theoretical ceiling.
    cfg_path = run_dir / "config.yaml"
    theoretical = None
    if cfg_path.is_file():
        try:
            import yaml
            cfg = yaml.safe_load(cfg_path.read_text())
            defaults = (cfg.get("defaults") or {})
            sf = int(defaults.get("lora_spreading_factor", 9))
            bw = float(defaults.get("lora_bandwidth", 125.0))
            cr = int(defaults.get("lora_coding_rate", 5))
            duty = float(defaults.get("lora_duty_cycle", 1.0))
            # Assume 242-byte payload (PACKET_SIZE default in config.h).
            airtime_ms = _airtime_ms(sf, bw, 242, cr=cr)
            # Per-node ceiling at 1.0 duty: 60_000 / airtime_ms packets/min.
            theoretical = {
                "airtime_ms_at_242B": airtime_ms,
                "per_node_max_pkt_per_min_at_duty": (60_000.0 * duty) / airtime_ms,
            }
        except Exception as e:  # pragma: no cover — diagnostic only
            theoretical = {"error": repr(e)}

    return {
        "offered_pkt_per_min": offered_pkt_per_min,
        "pdr": pdr,
        "delivered_pkt_per_min": (offered_pkt_per_min * pdr) if pdr is not None else None,
        "window_min": window_min,
        "theoretical": theoretical,
    }


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Compute capacity metrics for one run")
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    result = analyse(args.run_dir)
    out = args.run_dir / "capacity.json"
    out.write_text(json.dumps(result, indent=2, sort_keys=True))
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
