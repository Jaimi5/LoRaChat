"""Synthetic run directories for the sleep-tooling tests.

`make_run(tmp, logs={node: [(t_s, line), ...]}, ...)` writes a run dir laid out
like runner.run_batch's (config.yaml, lifecycle.json, logs/monitor-dev-*.log)
with gateway-style `[YYYY-MM-DD HH:MM:SS.mmm] ` prefixes. Times are seconds
after T0 (2026-10-10 10:00:00 UTC; lifecycle.json uses the same clock, so the
local-offset detection resolves to 0).
"""

from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

import yaml

T0 = datetime(2026, 10, 10, 10, 0, 0, tzinfo=timezone.utc)
SESSION = "test-20261010-100000-sleep_deep_SF9-r01"


def ts(t_s: float) -> str:
    return (T0 + timedelta(seconds=t_s)).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def iso(t_s: float) -> str:
    return (T0 + timedelta(seconds=t_s)).isoformat(timespec="milliseconds")


def lib(addr: str, msg: str, level: str = "INFO") -> str:
    """A LoRaMesher library line as the monitor captures it (ANSI colours)."""
    return f"\x1b[0;32m[{level}] [0x{addr}] {msg}\x1b[0m"


def app(msg: str, ms: int = 1000) -> str:
    return f"I ({ms}) LoRaMeshService: {msg}"


def slot(n: int, kind: str) -> str:
    return lib("0000", f"Slot {n} transition: type={kind}", "DEBUG")


def lifecycle(reset_s=-30.0, warmup_s=0.0, meas_s=(0.0, 3600.0), cooldown_s=60.0,
              clocks: tuple[dict, dict] | None = None) -> list[dict]:
    m0, m1 = meas_s
    ph = []
    if clocks:
        ph.append({"name": "clock-run-start", "start": iso(reset_s - 60), "end": iso(reset_s - 60),
                   "ok": True, **clocks[0]})
    ph += [
        {"name": "reset", "start": iso(reset_s), "end": iso(reset_s + 20), "ok": True, "rc": 0},
        {"name": "monitor-start", "start": iso(m0 - warmup_s - 1), "end": iso(m0 - warmup_s), "ok": True},
        {"name": "warmup", "start": iso(m0 - warmup_s), "end": iso(m0), "ok": True, "seconds": warmup_s},
        {"name": "measurement-start", "start": iso(m0), "end": iso(m0), "ok": True},
        {"name": "measurement", "start": iso(m0), "end": iso(m1), "ok": True, "seconds": m1 - m0},
        {"name": "measurement-end", "start": iso(m1), "end": iso(m1), "ok": True},
        {"name": "cooldown", "start": iso(m1), "end": iso(m1 + cooldown_s), "ok": True, "seconds": cooldown_s},
        {"name": "monitor-stop", "start": iso(m1 + cooldown_s), "end": iso(m1 + cooldown_s + 5), "ok": True},
        {"name": "collect-logs", "start": iso(m1 + cooldown_s + 6), "end": iso(m1 + cooldown_s + 9), "ok": True},
    ]
    if clocks:
        ph.append({"name": "clock-run-end", "start": iso(m1 + cooldown_s + 10),
                   "end": iso(m1 + cooldown_s + 10), "ok": True, **clocks[1]})
    return ph


def make_run(root: Path, logs: dict[str, list[tuple[float, str]]], defaults: dict | None = None,
             devices: dict | None = None, phases: list[dict] | None = None,
             name: str = SESSION, batch: str = "test_batch") -> Path:
    run = root / batch / name
    (run / "logs").mkdir(parents=True)
    devs = devices or {n: {"lora_power": 2} for n in logs}
    cfg = {"experiment": name, "defaults": {"lora_spreading_factor": 9, "lora_manager_id": "0x006C",
                                            **(defaults or {})},
           "devices": devs}
    (run / "config.yaml").write_text(yaml.safe_dump(cfg, sort_keys=False))
    (run / "lifecycle.json").write_text(json.dumps(phases if phases is not None else lifecycle()))
    for node, lines in logs.items():
        p = run / "logs" / f"monitor-dev-{name}-{node}.log"
        p.write_text("".join(f"[{ts(t)}] {line}\n" for t, line in sorted(lines, key=lambda x: x[0])))
    return run
