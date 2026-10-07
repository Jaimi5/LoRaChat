#!/usr/bin/env python3
"""Pass/fail gates for the LoRaMesher 2.0.0 testbed campaign.

Each gate turns one tier of the plan into machine-checkable results:

    g_minus1  local build logs (lib sha, RAM/flash headroom)
    g0        smoke run(s): flash, boot, crashes, LDRO, join, NM, routing, PDR
    g1        core regression vs the July 134ae25 runs (computed, not hard-coded)
    g2        feature/stress cells (reliable, group, Stop/Start, NM failover, ...)

Every gate returns {"gate", "status", "checks": [...], ...} where status is the
worst check status: FAIL > INCOMPLETE > GREY > PASS. A check never raises: a
metric that cannot be computed (missing log lines, analysis module not yet
present) yields INCOMPLETE with the reason, so the campaign keeps going and the
report shows exactly what was not verified.

CLI (paths relative to scripts/testbed/ or absolute):
    gates.py g_minus1 build1.log [build2.log ...]
    gates.py g0 runs/lm200_smoke [--expect-sha 764b893]
    gates.py g1 formation=runs/lm200_formation 13node=runs/lm200_13node \\
                reach16=runs/lm200_reach16 dataslots=runs/lm200_dataslots
         (KIND=NEW_DIR[:BASELINE_DIR]; baseline defaults to campaign.yaml)
    gates.py g2 runs/lm200_features
  common: --config lm200/campaign.yaml --out <json> --expect-sha <sha>
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
import statistics
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

TESTBED = Path(__file__).resolve().parents[1]
if str(TESTBED) not in sys.path:
    sys.path.insert(0, str(TESTBED))

import yaml  # noqa: E402

from analysis import app_pdr as _app_pdr  # noqa: E402
from analysis import formation as _formation  # noqa: E402
from analysis import superframe as _superframe  # noqa: E402
from analysis.check_reset import check_run as _check_reset  # noqa: E402
from analysis.parse_logs import is_v2_run, parse_run  # noqa: E402

PASS, GREY, INCOMPLETE, FAIL = "PASS", "GREY", "INCOMPLETE", "FAIL"
_RANK = {PASS: 0, GREY: 1, INCOMPLETE: 2, FAIL: 3}
SUMMARY_VERSION = 4  # bump to invalidate cached run summaries

# ── small helpers ─────────────────────────────────────────────────────────────


def chk(name, status, value=None, threshold=None, detail="", run=None, blocker=None):
    c = {"name": name, "status": status, "value": value, "threshold": threshold,
         "detail": detail}
    if run:
        c["run"] = run
    if blocker is not None:
        c["blocker"] = blocker
    return c


def worst(statuses) -> str:
    out = PASS
    for s in statuses:
        if _RANK.get(s, 0) > _RANK[out]:
            out = s
    return out


def gate_result(gate: str, checks: list[dict], **extra) -> dict:
    return {"gate": gate, "status": worst(c["status"] for c in checks) if checks else INCOMPLETE,
            "generated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "checks": checks, **extra}


def load_config(path: Path | None) -> dict:
    p = path or (TESTBED / "lm200" / "campaign.yaml")
    with open(p) as f:
        return yaml.safe_load(f) or {}


def tpath(p) -> Path:
    p = Path(p)
    return p if p.is_absolute() else (TESTBED / p)


def dig(d, dotted: str, default=None):
    """Nested get with dotted keys; tolerant of None/missing levels."""
    cur = d
    for k in dotted.split("."):
        if not isinstance(cur, dict) or k not in cur:
            return default
        cur = cur[k]
    return cur


def first_of(d, *keys):
    """First non-None value among alternative dotted keys (contract tolerance)."""
    for k in keys:
        v = dig(d, k)
        if v is not None:
            return v
    return None


_RUNID_RE = re.compile(r"^(?:[A-Za-z][\w-]*?-)?\d{8}-\d{6}-(?P<cell>.+)-r(?P<rep>\d+)$")


def cell_of(run_dir: Path) -> tuple[str, int] | None:
    m = _RUNID_RE.match(run_dir.name)
    return (m.group("cell"), int(m.group("rep"))) if m else None


def run_dirs(path: Path, include_warmup: bool = False) -> list[Path]:
    """A run dir itself, or every valid run dir under a batch dir (sorted).
    Skips moved-aside failures (*.FAILED-*, *.INVALID*, *.stale-*)."""
    path = tpath(path)
    if (path / "logs").is_dir() or (path / "config.yaml").is_file() and cell_of(path):
        return [path]
    out = []
    if not path.is_dir():
        return out
    for d in sorted(path.iterdir()):
        if not d.is_dir() or any(t in d.name for t in (".FAILED", "FAIL-", ".INVALID", ".stale", ".STRAY")):
            continue
        c = cell_of(d)
        if c is None:
            continue
        if c[1] == 0 and not include_warmup:
            continue
        out.append(d)
    return out


def _open(path: Path):
    if path.suffix == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open("r", encoding="utf-8", errors="replace")


def _short_id(path: Path) -> str:
    n = path.name
    for suf in (".gz", ".log"):
        if n.endswith(suf):
            n = n[: -len(suf)]
    return n.rsplit("-", 1)[-1].upper()


# ── run config ────────────────────────────────────────────────────────────────


def run_config(run_dir: Path) -> dict:
    cfg_path = run_dir / "config.yaml"
    raw = {}
    if cfg_path.is_file():
        try:
            raw = yaml.safe_load(cfg_path.read_text()) or {}
        except Exception:
            raw = {}
    defaults = raw.get("defaults") or {}
    devices = {str(k).upper(): (v or {}) for k, v in (raw.get("devices") or {}).items()}
    active = {k for k, v in devices.items()
              if str(v.get("node_active", 1)).lower() not in ("0", "false")}

    def per_dev(key, default=0):
        return {k: v.get(key, defaults.get(key, default)) for k, v in devices.items()}

    return {
        "devices": sorted(devices),
        "active": sorted(active),
        "sinks": sorted(k for k in active if str(devices[k].get("wifi_ssid", "nowifi")) != "nowifi"),
        "sf": defaults.get("lora_spreading_factor"),
        "dds": defaults.get("lora_default_data_slots", 1),
        "max_ds": defaults.get("lora_max_data_slots"),
        "sim_pdr_compare": defaults.get("sim_pdr_compare", 0),
        "sim_reliable": per_dev("sim_reliable"),
        "sim_group": per_dev("sim_group"),
        "sim_stopstart": per_dev("sim_stopstart"),
        "sim_nm_failover_ms": defaults.get("sim_nm_failover_ms", 0),
    }


# ── raw log scan (crashes, LDRO, build env, stacks, upload logs) ──────────────

_CRASH_PATTERNS = [
    "Guru Meditation Error", "abort() was called", "Stack canary watchpoint",
    "stack overflow", "CORRUPT HEAP", "Task watchdog got triggered",
    "Brownout detector was triggered", "assert failed", "LoadProhibited",
    "StoreProhibited", "InstrFetchProhibited", "IllegalInstruction",
]
_TS_RE = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}(?:\.\d+)?)\]")
_RE_BUILD_ENV = re.compile(r"Build environment name:\s*(\S+)")
_RE_APP_VER = re.compile(r"App version:\s*(\S+)")
_RE_JOIN_SLOT = re.compile(r"Join request scheduled in discovery slot (\d+)")
_RE_STACK = re.compile(r"STACK\[([^\]]+)\]\s+total=(\d+)\s+free=(\d+)")
_RE_HEAP = re.compile(r"FREE HEAP:\s*(\d+)")
_RE_LIBSHA = re.compile(r"LoRaMesher @ (\S+)")
_RE_RADIOLIB = re.compile(r"RadioLib @ (\S+)")
_RE_OK = re.compile(r"^OK: (\S+)")
_RE_FAILED = re.compile(r"^FAILED: (\S+)")


def _ts(line: str) -> float | None:
    m = _TS_RE.match(line)
    if not m:
        return None
    s = m.group(1)
    try:
        fmt = "%Y-%m-%d %H:%M:%S.%f" if "." in s else "%Y-%m-%d %H:%M:%S"
        return datetime.strptime(s, fmt).timestamp()
    except ValueError:
        return None


def scan_device_logs(run_dir: Path) -> dict:
    """One pass over each monitor log for everything the gates grep for."""
    out: dict[str, dict] = {}
    logs = run_dir / "logs"
    if not logs.is_dir():
        return out
    for p in sorted(list(logs.glob("monitor-dev-*.log")) + list(logs.glob("monitor-dev-*.log.gz"))):
        node = _short_id(p)
        d = {"lines": 0, "boots": 0, "expected_reboots": 0, "unexpected_reboots": 0,
             "crashes": [], "toa_mismatch": 0, "build_env": None, "app_version": None,
             "join_slots": [], "stack_min_free": {}, "heap_min": None,
             "nm_failover": 0, "last_ts": None, "first_ts": None}
        last_failover_ts = None
        try:
            with _open(p) as fh:
                for line in fh:
                    d["lines"] += 1
                    t = _ts(line)
                    if t is not None:
                        d["last_ts"] = t
                        if d["first_ts"] is None:
                            d["first_ts"] = t
                    if "rst:0x" in line:
                        d["boots"] += 1
                        if d["boots"] > 1:
                            if last_failover_ts is not None and t is not None and t - last_failover_ts < 60:
                                d["expected_reboots"] += 1
                            elif "--- RESET via gw-reset.sh" in line:
                                d["expected_reboots"] += 1
                            else:
                                d["unexpected_reboots"] += 1
                        continue
                    if "--- RESET via gw-reset.sh" in line:
                        # deliberate host reset: the next rst:0x is expected
                        last_failover_ts = t
                        continue
                    if "NM_FAILOVER" in line:
                        d["nm_failover"] += 1
                        last_failover_ts = t
                    if "Time-on-air looks wrong" in line:
                        d["toa_mismatch"] += 1
                    for pat in _CRASH_PATTERNS:
                        if pat in line:
                            if len(d["crashes"]) < 5:
                                d["crashes"].append(line.strip()[:240])
                            else:
                                d["crashes"].append("…")
                                d["crashes"] = d["crashes"][:6]
                            break
                    if d["build_env"] is None and (m := _RE_BUILD_ENV.search(line)):
                        d["build_env"] = m.group(1)
                    if d["app_version"] is None and (m := _RE_APP_VER.search(line)):
                        d["app_version"] = m.group(1)
                    if (m := _RE_JOIN_SLOT.search(line)):
                        d["join_slots"].append(int(m.group(1)))
                    if (m := _RE_STACK.search(line)):
                        task, free = m.group(1), int(m.group(3))
                        prev = d["stack_min_free"].get(task)
                        d["stack_min_free"][task] = free if prev is None else min(prev, free)
                    if (m := _RE_HEAP.search(line)):
                        v = int(m.group(1))
                        d["heap_min"] = v if d["heap_min"] is None else min(d["heap_min"], v)
        except (OSError, EOFError) as e:
            d["read_error"] = str(e)
        out[node] = d
    return out


def scan_upload_logs(run_dir: Path) -> dict | None:
    """Parse logs/GW-*-upload.log. None when the run had no upload (reset-only rep)."""
    logs = sorted((run_dir / "logs").glob("GW-*-upload.log")) if (run_dir / "logs").is_dir() else []
    if not logs:
        return None
    ok, failed, shas, chip, missing_gw = set(), set(), Counter(), 0, []
    radiolib: dict[str, list[str]] = {}
    lm_gw: dict[str, list[str]] = {}
    for p in logs:
        gw = p.name.split("-upload")[0]
        try:
            text = p.read_text(errors="replace")
        except OSError:
            missing_gw.append(p.name)
            continue
        for line in text.splitlines():
            if (m := _RE_OK.match(line)):
                ok.add(m.group(1).rsplit("-", 1)[-1].upper())
            elif (m := _RE_FAILED.match(line)):
                failed.add(m.group(1).rsplit("-", 1)[-1].upper())
            if (m := _RE_LIBSHA.search(line)):
                shas[m.group(1)] += 1
                if m.group(1) not in lm_gw.setdefault(gw, []):
                    lm_gw[gw].append(m.group(1))
            if (m := _RE_RADIOLIB.search(line)):
                vers = radiolib.setdefault(gw, [])
                if m.group(1) not in vers:
                    vers.append(m.group(1))
            if "chip stopped responding" in line or "Failed to connect to ESP32" in line:
                chip += 1
    return {"gateways": len(logs), "ok": sorted(ok), "failed": sorted(failed - ok),
            "lib_versions": dict(shas), "lib_per_gw": lm_gw, "radiolib": radiolib, "chip_errors": chip,
            "unreadable": missing_gw}


RADIOLIB_PROBE: Path | None = None   # <campaign_dir>/radiolib.json (informational probe)
EXPECTED_RADIOLIB: str | None = None  # campaign.yaml expected_radiolib, set by main()


def radiolib_problems(upload: dict, expected: str | None) -> list[str]:
    """platformio.ini pins RadioLib, so every gateway that built LoRaMesher must show
    exactly `RadioLib @ <expected>` in its dependency graph. Returns the problems."""
    if not expected or not upload:
        return []
    probs = []
    rl = upload.get("radiolib") or {}
    for gw, libs in sorted((upload.get("lib_per_gw") or {}).items()):
        vers = rl.get(gw) or []
        if not vers:
            probs.append(f"{gw}: no 'RadioLib @' line although LoRaMesher {libs} was built")
        elif any(v != expected for v in vers):
            probs.append(f"{gw}: RadioLib {vers} != {expected}")
    return probs


def check_radiolib(upload: dict | None, run=None, name="radiolib", expected: str | None = "__default__") -> dict:
    exp = EXPECTED_RADIOLIB if expected == "__default__" else expected
    rl = (upload or {}).get("radiolib") or {}
    if not exp:
        versions = sorted({v for vs in rl.values() for v in vs})
        st = GREY if len(versions) > 1 else PASS
        return chk(name, st, value=rl or None, run=run,
                   detail="no expected_radiolib configured" + (f"; versions differ: {versions}" if st == GREY else ""))
    probs = radiolib_problems(upload or {}, exp)
    if probs:
        return chk(name, FAIL, value=rl, threshold=exp, run=run, blocker=True, detail="; ".join(probs))
    if not (upload or {}).get("lib_per_gw"):
        return chk(name, INCOMPLETE, threshold=exp, run=run, detail="no LoRaMesher build in upload logs")
    return chk(name, PASS, value=exp, threshold=exp, run=run, detail=f"pinned version on {len(rl)} gateway(s)")


def batch_upload_merged(path: Path) -> dict:
    """Merge lib/RadioLib per gateway over every upload in a batch dir."""
    merged = {"lib_per_gw": {}, "radiolib": {}}
    for r in run_dirs(path, include_warmup=True):
        u = scan_upload_logs(r) or {}
        for key in ("lib_per_gw", "radiolib"):
            for gw, vs in (u.get(key) or {}).items():
                for v in vs:
                    if v not in merged[key].setdefault(gw, []):
                        merged[key][gw].append(v)
    return merged


def batch_upload_shas(path: Path) -> dict:
    out = Counter()
    for r in run_dirs(path, include_warmup=True):
        u = scan_upload_logs(r)
        if u:
            out.update(u["lib_versions"])
    return dict(out)


# ── lm200_metrics bridge (analysis teammate's module; optional) ───────────────

_LM200_METRICS = TESTBED / "analysis" / "lm200_metrics.py"


def lm200_metrics(run_dir: Path, cache_dir: Path) -> dict | None:
    if not _LM200_METRICS.is_file():
        return None
    cache_dir.mkdir(parents=True, exist_ok=True)
    out = cache_dir / f"lm200_metrics-{_key(run_dir)}.json"
    if not out.is_file():
        try:
            subprocess.run([sys.executable, str(_LM200_METRICS), str(run_dir), "--json", str(out)],
                           cwd=TESTBED, check=True, capture_output=True, timeout=1800)
        except Exception as e:  # noqa: BLE001
            return {"_error": f"lm200_metrics failed: {e}"}
    try:
        return json.loads(out.read_text())
    except Exception as e:  # noqa: BLE001
        return {"_error": f"lm200_metrics output unreadable: {e}"}


# ── per-run summary (cached outside the run dir; baseline dirs stay read-only) ─


def _key(run_dir: Path) -> str:
    logs = run_dir / "logs"
    mt = max((p.stat().st_mtime for p in logs.iterdir()), default=0) if logs.is_dir() else 0
    h = hashlib.sha1(f"{run_dir.resolve()}|{mt}|{SUMMARY_VERSION}".encode()).hexdigest()[:16]
    return f"{run_dir.name[:60]}-{h}"


def _safe(fn, *a, **kw):
    try:
        return fn(*a, **kw), None
    except Exception as e:  # noqa: BLE001
        return None, f"{type(e).__name__}: {e}"


def _full_formation(run_dir: Path, active: list[str]) -> dict:
    """Join / routing-table completeness over the FULL log (formation.analyse
    windows to the measurement window, which misses joins made during warmup)."""
    events = parse_run(run_dir, window=(None, None))
    expected = set(active)
    joined = _formation._compute_join(events)
    rt = _formation._compute_rt_complete(events, expected, is_v2=is_v2_run(events))
    res = {"joined": sorted(joined), "rt_complete": sorted(rt)}
    if hasattr(_formation, "nm_timeline") and hasattr(_formation, "nm_overlap"):
        ov = _formation.nm_overlap(_formation.nm_timeline(events, (None, None)))
        res["max_concurrent_nm_full"] = ov["max_concurrent_nm"]
        res["dual_nm_seconds_full"] = ov["dual_nm_seconds"]
    return res


def summarize_run(run_dir: Path, cache_dir: Path, with_metrics: bool = False) -> dict:
    cache_dir.mkdir(parents=True, exist_ok=True)
    cpath = cache_dir / f"summary-{_key(run_dir)}{'-m' if with_metrics else ''}.json"
    if cpath.is_file():
        try:
            return json.loads(cpath.read_text())
        except Exception:  # noqa: BLE001
            pass
    cfg = run_config(run_dir)
    cell = cell_of(run_dir)
    s = {"run": run_dir.name, "path": str(run_dir), "cell": cell[0] if cell else None,
         "rep": cell[1] if cell else None, "cfg": cfg, "errors": {}}
    n_logs, missing_boot = _check_reset(run_dir)
    s["boot"] = {"n_logs": n_logs, "missing_boot": missing_boot}
    s["scan"], err = _safe(scan_device_logs, run_dir)
    if err:
        s["errors"]["scan"] = err
    s["upload"], err = _safe(scan_upload_logs, run_dir)
    if err:
        s["errors"]["upload"] = err
    fm, err = _safe(_formation.analyse, run_dir)
    if err:
        s["errors"]["formation"] = err
    s["formation"] = {k: (fm or {}).get(k) for k in (
        "network_convergence_at", "n_active_devices", "n_nodes_joined", "n_nodes_rt_complete",
        "join_retries_total", "flaps_total", "flaps", "rejoins_total", "max_concurrent_nm",
        "dual_nm_seconds", "nm_nodes", "nm_intervals")}
    s["formation_full"], err = _safe(_full_formation, run_dir, cfg["active"])
    if err:
        s["errors"]["formation_full"] = err
    if cfg.get("sim_pdr_compare"):
        app, err = _safe(_app_pdr.analyse, run_dir)
        if err:
            s["errors"]["app_pdr"] = err
        s["app"] = {"fair_pdr": dig(app, "totals.pdr_delivered_intended"),
                    "pdr": dig(app, "totals.pdr"), "sent": dig(app, "totals.sent"),
                    "delivered": dig(app, "totals.delivered"),
                    "intended": dig(app, "totals.intended"),
                    "participation": dig(app, "totals.sender_participation"),
                    "latency_p95_ms": dig(app, "latency_ms_overall.p95"),
                    "per_flow": (app or {}).get("pdr_per_flow")}
    sfx, err = _safe(_superframe.extract, run_dir, allow_slot_wrap=True)
    if err:
        s["errors"]["superframe"] = err
    s["superframe"] = sfx
    if with_metrics:
        s["metrics"] = lm200_metrics(run_dir, cache_dir)
    cpath.write_text(json.dumps(s, indent=1, default=list))
    return s


# ── shared checks ─────────────────────────────────────────────────────────────


def check_upload(s: dict, expect_sha: str | None) -> list[dict]:
    u = s.get("upload")
    run = s["run"]
    if u is None:
        return [chk("flash", PASS, detail="reset-only repetition (no upload in this run)", run=run)]
    out = []
    devices = set(s["cfg"]["devices"])
    not_ok = sorted(devices - set(u["ok"]))
    if u["failed"] or not_ok:
        out.append(chk("flash", FAIL, value=f"{len(u['ok'])}/{len(devices)}",
                       detail=f"failed={u['failed']} not-OK={not_ok} chip_errors={u['chip_errors']}",
                       run=run, blocker=True))
    else:
        out.append(chk("flash", PASS, value=f"{len(u['ok'])}/{len(devices)}", run=run))
    if expect_sha:
        vers = u["lib_versions"]
        bad = {v: n for v, n in vers.items() if not v.split("+sha.")[-1].startswith(expect_sha)}
        if not vers:
            out.append(chk("lib_sha", INCOMPLETE, detail="no 'LoRaMesher @' line in upload logs", run=run))
        elif bad:
            out.append(chk("lib_sha", FAIL, value=vers, threshold=expect_sha, run=run, blocker=True,
                           detail="gateways built a different LoRaMesher than expected"))
        else:
            out.append(chk("lib_sha", PASS, value=sorted(vers), threshold=expect_sha, run=run))
    out.append(check_radiolib(u, run=run))
    return out


def check_health(s: dict, expected_env: str | None = None,
                 allow_failover_reboots: bool = False) -> list[dict]:
    """Boot, crash, reboot, LDRO and build-env checks over all board logs."""
    run = s["run"]
    out = []
    devices = s["cfg"]["devices"]
    scan = s.get("scan") or {}
    nb = s["boot"]
    missing_logs = sorted(set(devices) - set(scan))
    if nb["n_logs"] == 0:
        return [chk("boot", FAIL, detail="no monitor logs at all", run=run, blocker=True)]
    if missing_logs or nb["missing_boot"]:
        out.append(chk("boot", FAIL, value=f"{len(scan) - len(nb['missing_boot'])}/{len(devices)}",
                       detail=f"no log={missing_logs} no boot banner={nb['missing_boot']}",
                       run=run, blocker=True))
    else:
        out.append(chk("boot", PASS, value=f"{len(devices)}/{len(devices)}", run=run))
    crashes = {n: d["crashes"] for n, d in scan.items() if d["crashes"]}
    out.append(chk("crashes", FAIL if crashes else PASS, value=len(crashes),
                   detail="; ".join(f"{n}: {c[0]}" for n, c in sorted(crashes.items()))[:600],
                   run=run, blocker=bool(crashes)))
    unexp = {n: d["unexpected_reboots"] for n, d in scan.items() if d["unexpected_reboots"]}
    expd = {n: d["expected_reboots"] for n, d in scan.items() if d["expected_reboots"]}
    if unexp:
        out.append(chk("reboots", FAIL, value=unexp, detail="unexpected reboot(s) during the run",
                       run=run, blocker=True))
    else:
        det = f"expected (NM failover / host reset): {expd}" if expd else ""
        out.append(chk("reboots", PASS, value=0, detail=det, run=run))
    if not allow_failover_reboots and any(d["nm_failover"] for d in scan.values()):
        out.append(chk("nm_failover_flag", FAIL, detail="NM_FAILOVER fired outside the failover cell",
                       run=run, blocker=True))
    toa = {n: d["toa_mismatch"] for n, d in scan.items() if d["toa_mismatch"]}
    out.append(chk("toa_ldro", FAIL if toa else PASS, value=sum(toa.values()),
                   detail=f"'Time-on-air looks wrong' on {sorted(toa)}" if toa else "",
                   run=run, blocker=bool(toa)))
    if expected_env:
        envs = {n: d["build_env"] for n, d in scan.items()}
        wrong = {n: e for n, e in envs.items() if e and e != expected_env}
        none = sorted(n for n, e in envs.items() if not e)
        if wrong:
            out.append(chk("build_env", FAIL, value=wrong, threshold=expected_env, run=run, blocker=True))
        elif none:
            out.append(chk("build_env", GREY, threshold=expected_env, run=run,
                           detail=f"no 'Build environment name' captured on {none}"))
        else:
            out.append(chk("build_env", PASS, value=expected_env, run=run))
    return out


def check_join(s: dict, remote: set[str], require_remote: bool,
               name: str = "join") -> list[dict]:
    run = s["run"]
    ff = s.get("formation_full")
    if not ff:
        return [chk(name, INCOMPLETE, detail=s["errors"].get("formation_full", "no formation data"), run=run)]
    active = set(s["cfg"]["active"])
    rt = set(ff["rt_complete"])
    # A complete routing table is proof of membership too: serial capture can drop
    # the one "state changed to 3" line (seen in July 134ae25 runs).
    joined = set(ff["joined"]) | rt
    out = []
    miss = sorted(active - joined)
    miss_main = [n for n in miss if n not in remote]
    if miss_main:
        out.append(chk(name, FAIL, value=f"{len(joined & active)}/{len(active)}",
                       detail=f"never joined: {miss}", run=run, blocker=True))
    elif miss:
        out.append(chk(name, GREY if not require_remote else FAIL,
                       value=f"{len(joined & active)}/{len(active)}",
                       detail=f"remote node(s) never joined (long-link margin?): {miss}", run=run))
    else:
        out.append(chk(name, PASS, value=f"{len(active)}/{len(active)}", run=run))
    miss_rt = sorted(active - rt)
    miss_rt_main = [n for n in miss_rt if n not in remote]
    if miss_rt_main:
        st = FAIL if len(miss_rt_main) > 1 else GREY
        out.append(chk("routing_tables", st, value=f"{len(rt & active)}/{len(active)}",
                       detail=f"routing table never complete on {miss_rt}", run=run))
    elif miss_rt:
        out.append(chk("routing_tables", GREY, value=f"{len(rt & active)}/{len(active)}",
                       detail=f"incomplete only on remote node(s) {miss_rt}", run=run))
    else:
        out.append(chk("routing_tables", PASS, value=f"{len(active)}/{len(active)}", run=run))
    return out


def check_nm(s: dict, expect_failover: bool = False) -> dict:
    run = s["run"]
    ff = s.get("formation_full") or {}
    mx = ff.get("max_concurrent_nm_full", dig(s, "formation.max_concurrent_nm"))
    dual = ff.get("dual_nm_seconds_full", dig(s, "formation.dual_nm_seconds"))
    if mx is None:
        return chk("single_nm", INCOMPLETE, detail="max_concurrent_nm not available "
                   "(analysis/formation.py without NM timeline)", run=run)
    if mx > 1:
        return chk("single_nm", FAIL, value=mx, threshold=1, run=run, blocker=True,
                   detail=f"two managers at once for {dual:.0f} s" if dual is not None else "")
    if mx == 0:
        return chk("single_nm", FAIL if not expect_failover else GREY, value=0, threshold=1, run=run,
                   detail="no node ever became network manager")
    return chk("single_nm", PASS, value=mx, threshold=1, run=run)


# ── G-1: local build ─────────────────────────────────────────────────────────

_RE_RAM = re.compile(r"RAM:\s+\[.*?\]\s+([\d.]+)%")
_RE_FLASH = re.compile(r"Flash:\s+\[.*?\]\s+([\d.]+)%")


def g_minus1(build_logs: list[Path], cfg: dict, expect_sha: str | None) -> dict:
    th = dig(cfg, "thresholds.g_minus1", {}) or {}
    checks = []
    if not build_logs:
        return gate_result("g_minus1", [chk("build_logs", INCOMPLETE, detail="no build logs given")])
    for p in build_logs:
        p = tpath(p)
        try:
            text = p.read_text(errors="replace")
        except OSError as e:
            checks.append(chk("build", FAIL, detail=str(e), run=p.name))
            continue
        ok = "[SUCCESS]" in text or re.search(r"=+ \[?SUCCESS\]? Took", text) is not None
        checks.append(chk("build", PASS if ok else FAIL, run=p.name,
                          detail="" if ok else "no [SUCCESS] marker", blocker=not ok))
        shas = set(_RE_LIBSHA.findall(text))
        if expect_sha:
            if not shas:
                checks.append(chk("lib_sha", INCOMPLETE, run=p.name, detail="no 'LoRaMesher @' line"))
            else:
                good = all(s.split("+sha.")[-1].startswith(expect_sha) for s in shas)
                checks.append(chk("lib_sha", PASS if good else FAIL, value=sorted(shas),
                                  threshold=expect_sha, run=p.name))
        for name, rx, key in (("ram", _RE_RAM, "max_ram_pct"), ("flash", _RE_FLASH, "max_flash_pct")):
            m = rx.findall(text)
            if not m:
                checks.append(chk(name, INCOMPLETE, run=p.name, detail="no usage line"))
                continue
            v = float(m[-1])
            lim = th.get(key)
            checks.append(chk(name, PASS if lim is None or v <= lim else FAIL, value=v,
                              threshold=lim, run=p.name))
    return gate_result("g_minus1", checks)


# ── G0: smoke ────────────────────────────────────────────────────────────────


def _superframe_cached(run_dir: Path, cache_dir: Path) -> dict | None:
    """superframe.extract only (cheap compared with a full run summary)."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    p = cache_dir / f"superframe-{_key(run_dir)}.json"
    if p.is_file():
        return json.loads(p.read_text())
    x, _ = _safe(_superframe.extract, run_dir, allow_slot_wrap=True)
    p.write_text(json.dumps(x))
    return x


def _superframe_ref(path: Path, cache_dir: Path, sf, cell_hint: str | None = None) -> dict | None:
    """Modal total_slots / superframe_s of baseline runs at the given SF."""
    tots, sfs = [], []
    for r in run_dirs(path):
        c = cell_of(r)
        if cell_hint and c and cell_hint not in c[0]:
            continue
        if run_config(r).get("sf") != sf:
            continue
        x = _superframe_cached(r, cache_dir)
        if not x:
            continue
        tots.append(x.get("total_slots"))
        if x.get("superframe_s"):
            sfs.append(round(x["superframe_s"], 1))
    tots = [t for t in tots if t]
    if not tots:
        return None
    return {"total_slots": Counter(tots).most_common(1)[0][0],
            "superframe_s": Counter(sfs).most_common(1)[0][0] if sfs else None,
            "n": len(tots), "source": str(path)}


def g0(paths: list[Path], cfg: dict, expect_sha: str | None, cache_dir: Path) -> dict:
    th = dig(cfg, "thresholds.g0", {}) or {}
    remote = {str(x).upper() for x in cfg.get("remote_nodes", [])}
    env = dig(cfg, "envs.new")
    checks = []
    runs = [r for p in paths for r in run_dirs(p)]
    if not runs:
        return gate_result("g0", [chk("runs", INCOMPLETE, detail=f"no run dirs under {paths}")])
    ref = None
    base13 = dig(cfg, "baseline.lm200_13node")
    for r in runs:
        s = summarize_run(r, cache_dir)
        checks += check_upload(s, expect_sha)
        checks += check_health(s, expected_env=env)
        checks += check_join(s, remote, require_remote=False)
        checks.append(check_nm(s))
        # Fair app PDR on the designed senders.
        fp = dig(s, "app.fair_pdr")
        lim = th.get("min_fair_pdr", 0.95)
        if fp is None:
            checks.append(chk("fair_pdr", INCOMPLETE, detail=s["errors"].get("app_pdr", "no APP_TX/APP_RX"),
                              run=s["run"]))
        else:
            checks.append(chk("fair_pdr", PASS if fp >= lim else FAIL, value=round(fp, 4),
                              threshold=lim, run=s["run"], blocker=fp < lim))
        # Superframe vs the July 13-node run at the same SF (2.0.0 adds 1 B per RT entry).
        sfx = s.get("superframe")
        if ref is None and base13:
            ref = _superframe_ref(tpath(base13), cache_dir, s["cfg"].get("sf")) or {}
        if not sfx:
            checks.append(chk("superframe", GREY, run=s["run"], detail="no superframe lines/slot wraps"))
        elif not ref:
            checks.append(chk("superframe", GREY, value=sfx.get("total_slots"), run=s["run"],
                              detail="no July reference at this SF"))
        else:
            rel = abs(sfx["total_slots"] - ref["total_slots"]) / ref["total_slots"]
            st = PASS if rel <= th.get("superframe_grey_rel", 0.25) else GREY
            checks.append(chk("superframe_total_slots", st, value=sfx["total_slots"],
                              threshold=ref["total_slots"], run=s["run"],
                              detail=f"July modal {ref['total_slots']} slots (n={ref['n']}); rel diff {rel:.0%}"))
        # 2.0.0 join path: JOIN_REQUEST in a random EVEN discovery slot.
        slots = [x for d in (s.get("scan") or {}).values() for x in d["join_slots"]]
        if not slots:
            checks.append(chk("join_even_slot", GREY, run=s["run"],
                              detail="no 'Join request scheduled in discovery slot' lines (log level / old lib?)"))
        else:
            odd = [x for x in slots if x % 2]
            checks.append(chk("join_even_slot", PASS if not odd else GREY, value=len(slots), run=s["run"],
                              detail=f"odd slots seen: {sorted(set(odd))}" if odd else
                              f"{len(set(slots))} distinct even slots"))
    return gate_result("g0", checks)


# ── G1: regression vs July ───────────────────────────────────────────────────


def _cells(paths_runs: list[Path], cache_dir: Path) -> dict[str, list[dict]]:
    by = defaultdict(list)
    for r in paths_runs:
        s = summarize_run(r, cache_dir)
        by[s["cell"]].append(s)
    return dict(by)


def _has_logs(r: Path) -> bool:
    logs = r / "logs"
    return logs.is_dir() and any(logs.glob("monitor-dev-*"))


def _cells_with_logs(runs: list[Path], cache_dir: Path) -> tuple[dict[str, list[dict]], int]:
    good = [r for r in runs if _has_logs(r)]
    return _cells(good, cache_dir), len(runs) - len(good)


def _join_bad(s: dict, remote: set[str]) -> tuple[bool, bool]:
    """(main-cluster node never joined, remote node never joined) for one run."""
    ff = s.get("formation_full") or {}
    active = set(s["cfg"]["active"])
    joined = set(ff.get("joined", [])) | set(ff.get("rt_complete", []))
    miss = active - joined
    return bool(miss - remote), bool(miss & remote)


def _join_agg_check(name: str, new: list[dict], base: list[dict], remote: set[str]) -> dict:
    """Share of runs where some main-cluster node never joined, new vs July. The July
    runs are not perfect (e.g. a node absent for a whole 15-min SF7 cold start), so
    this compares rates instead of demanding 100%."""
    nb = [_join_bad(s, remote) for s in new]
    bb = [_join_bad(s, remote) for s in base]
    n_bad, b_bad = sum(m for m, _ in nb), sum(m for m, _ in bb)
    n_frac = n_bad / len(nb) if nb else 0.0
    b_frac = (b_bad / len(bb)) if bb else None
    rem_new = sum(r for _, r in nb)
    misses = sorted({n for s in new for n in (set(s["cfg"]["active"]) - set((s.get("formation_full") or {})
                                             .get("joined", [])) - set((s.get("formation_full") or {})
                                             .get("rt_complete", [])))})
    det = (f"runs with a main-cluster node never joined: new {n_bad}/{len(nb)}"
           + (f", July {b_bad}/{len(bb)}" if bb else ", no July runs")
           + (f"; runs with a remote node missing: {rem_new}/{len(nb)}" if rem_new else "")
           + (f"; missing nodes: {misses}" if misses else ""))
    if b_frac is None:
        st = PASS if n_bad == 0 else GREY
    elif n_frac <= b_frac:
        st = PASS
    elif n_bad >= 2 and n_frac > b_frac + 0.3:
        st = FAIL
    else:
        st = GREY
    c = chk(name, st, value=f"{n_bad}/{len(nb)}", threshold=f"<= July {b_bad}/{len(bb)}" if bb else None,
            detail=det)
    c["base_stats"] = {"bad_runs": b_bad, "runs": len(bb)}
    return c


def _stats(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    return {"n": len(xs), "mean": statistics.fmean(xs), "median": statistics.median(xs),
            "min": min(xs), "max": max(xs)}


def _pdr_check(name, new: dict | None, base: dict | None, drop: float, run=None):
    if not new:
        return chk(name, INCOMPLETE, detail="no fair PDR in new runs", run=run)
    if not base:
        return chk(name, GREY, value=round(new["mean"], 4), detail="no July baseline for this cell", run=run)
    floor = base["mean"] - drop
    v = new["mean"]
    det = (f"new mean {v:.3f} (n={new['n']}) vs July mean {base['mean']:.3f} "
           f"[{base['min']:.3f}..{base['max']:.3f}] (n={base['n']})")
    if v >= floor:
        return chk(name, PASS, value=round(v, 4), threshold=round(floor, 4), detail=det, run=run)
    if v >= base["min"] - drop / 2:
        return chk(name, GREY, value=round(v, 4), threshold=round(floor, 4), run=run,
                   detail=det + " — below tolerance but inside July run-to-run spread", )
    return chk(name, FAIL, value=round(v, 4), threshold=round(floor, 4), detail=det, run=run, blocker=True)


def g1(pairs: dict[str, tuple[Path, Path | None]], cfg: dict, expect_sha: str | None,
       cache_dir: Path) -> dict:
    th = dig(cfg, "thresholds.g1", {}) or {}
    remote = {str(x).upper() for x in cfg.get("remote_nodes", [])}
    checks: list[dict] = []
    baseline_summary: dict = {}
    ab_trigger = False
    for kind, (new_dir, base_dir) in pairs.items():
        new_runs = run_dirs(new_dir)
        if not new_runs:
            checks.append(chk(f"{kind}:runs", INCOMPLETE, detail=f"no runs in {new_dir}"))
            continue
        if expect_sha:
            shas = batch_upload_shas(new_dir)
            bad = [v for v in shas if not v.split("+sha.")[-1].startswith(expect_sha)]
            if not shas:
                checks.append(chk(f"{kind}:lib_sha", INCOMPLETE, threshold=expect_sha,
                                  detail="no 'LoRaMesher @' line in any upload log"))
            else:
                checks.append(chk(f"{kind}:lib_sha", FAIL if bad else PASS, value=sorted(shas),
                                  threshold=expect_sha, blocker=bool(bad)))
        checks.append(check_radiolib(batch_upload_merged(new_dir), name=f"{kind}:radiolib"))
        new, n_nolog = _cells_with_logs(new_runs, cache_dir)
        if n_nolog:
            checks.append(chk(f"{kind}:runs_without_logs", GREY, value=n_nolog,
                              detail="run dirs with no monitor logs were ignored"))
        base, _ = _cells_with_logs(run_dirs(base_dir), cache_dir) if base_dir else ({}, 0)
        dual = []
        for cell, ss in sorted(new.items()):
            for s in ss:
                checks += [c for c in check_health(s, expected_env=None) if c["status"] != PASS]
                c = check_nm(s)
                if c["status"] == FAIL and (c.get("value") or 0) > 1:
                    dual.append(c)
        checks += dual or [chk(f"{kind}:single_nm", PASS, value="no dual-NM interval in any run")]
        for cell, ss in sorted(new.items()):
            c = _join_agg_check(f"{kind}:{cell}:join_completeness", ss, base.get(cell, []), remote)
            checks.append(c)
            baseline_summary.setdefault(kind, {}).setdefault(cell, {})["join"] = c.get("base_stats")
        if kind in ("13node", "reach16"):
            drop = th.get("pdr_13node_drop" if kind == "13node" else "pdr_reach16_drop", 0.05)
            for cell, ss in sorted(new.items()):
                ns = _stats([dig(s, "app.fair_pdr") for s in ss])
                bs = _stats([dig(s, "app.fair_pdr") for s in base.get(cell, [])])
                baseline_summary.setdefault(kind, {}).setdefault(cell, {})["fair_pdr"] = bs
                c = _pdr_check(f"{kind}:{cell}:fair_pdr", ns, bs, drop)
                c["new_stats"], c["base_stats"] = ns, bs
                checks.append(c)
                if c["status"] == GREY and kind == "13node":
                    ab_trigger = True
        elif kind == "formation":
            margin = th.get("join_margin_superframes", 1)
            gf = th.get("join_retry_grey_factor", 2.0)
            for cell, ss in sorted(new.items()):
                bss = base.get(cell, [])
                sf = ss[0]["cfg"].get("sf")
                ref = _superframe_ref(base_dir, cache_dir, sf) if base_dir else None
                sf_s = (ref or {}).get("superframe_s")
                nconv = _stats([dig(s, "formation.network_convergence_at") for s in ss])
                bconv = _stats([dig(s, "formation.network_convergence_at") for s in bss])
                baseline_summary.setdefault(kind, {}).setdefault(cell, {}).update(
                    {"convergence_s": bconv, "superframe": ref})
                name = f"formation:{cell}:convergence"
                if not nconv:
                    checks.append(chk(name, INCOMPLETE, detail="no run reached full convergence"))
                elif not bconv or not sf_s:
                    checks.append(chk(name, GREY, value=round(nconv["median"], 1),
                                      detail="no July baseline/superframe for this cell"))
                else:
                    lim = bconv["median"] + margin * sf_s
                    det = (f"median {nconv['median']:.0f} s (n={nconv['n']}) vs July {bconv['median']:.0f} s "
                           f"[{bconv['min']:.0f}..{bconv['max']:.0f}] + {margin}×{sf_s:.1f} s superframe")
                    if nconv["median"] <= lim:
                        st = PASS
                    elif nconv["median"] <= bconv["max"]:
                        st, ab_trigger = GREY, True
                    else:
                        st = FAIL
                    checks.append(chk(name, st, value=round(nconv["median"], 1), threshold=round(lim, 1),
                                      detail=det))
                # Join retries (2.0.0 backoff vs 134ae25 retry lines).
                nr = sum(dig(s, "formation.join_retries_total") or 0 for s in ss) / max(len(ss), 1)
                br = (sum(dig(s, "formation.join_retries_total") or 0 for s in bss) / len(bss)) if bss else None
                st = PASS if br is None or nr <= max(gf * br, br + 3) else GREY
                checks.append(chk(f"formation:{cell}:join_retries_per_run", st, value=round(nr, 1),
                                  threshold=None if br is None else round(max(gf * br, br + 3), 1),
                                  detail="" if br is not None else "no July retry data"))
        elif kind == "dataslots":
            for cell, ss in sorted(new.items()):
                for s in ss:
                    sfx = s.get("superframe") or {}
                    want = s["cfg"].get("dds")
                    got = sfx.get("data_slots_granted")
                    if got is None:
                        checks.append(chk(f"dataslots:{cell}:granted", INCOMPLETE, run=s["run"],
                                          detail="no 'Active slots' line (NM superframe budget, DEBUG level)"))
                    else:
                        checks.append(chk(f"dataslots:{cell}:granted", PASS if abs(got - want) < 1e-6 else FAIL,
                                          value=round(got, 2), threshold=want, run=s["run"]))
        else:
            checks.append(chk(f"{kind}", INCOMPLETE, detail=f"unknown G1 kind {kind!r}"))
    return gate_result("g1", checks, ab_trigger=ab_trigger, baseline=baseline_summary)


# ── G2: features ─────────────────────────────────────────────────────────────


def _m(s, *keys):
    return first_of(s.get("metrics") or {}, *keys)


def _need(name, s, value, run, why="lm200_metrics key missing"):
    if value is None:
        err = dig(s, "metrics._error")
        section = name.split(":")[0]
        if not err and dig(s, f"metrics.{section}.present") is False:
            err = f"no {section} events in the logs (feature not exercised / flag not applied?)"
        return chk(name, INCOMPLETE, run=run, detail=err or (why if s.get("metrics") is not None
                                                            else "analysis/lm200_metrics.py not available"))
    return None


def _g2_cell(s: dict, cfg: dict) -> list[dict]:
    th = dig(cfg, "thresholds.g2", {}) or {}
    remote = {str(x).upper() for x in cfg.get("remote_nodes", [])}
    cell = (s.get("cell") or "").lower()
    run = s["run"]
    is_failover = cell.startswith("nm_failover")
    out = check_upload(s, None)
    out += check_health(s, allow_failover_reboots=is_failover)
    out.append(check_nm(s, expect_failover=is_failover))
    # Library stack headroom (2.0.0 logs real bytes; compare within 2.0.0 only).
    stacks = {}
    for d in (s.get("scan") or {}).values():
        for t, v in d["stack_min_free"].items():
            stacks[t] = v if t not in stacks else min(stacks[t], v)
    mfree = min(stacks.values()) if stacks else None
    lim = th.get("min_stack_free_bytes", 512)
    if mfree is None:
        out.append(chk("stack_free", GREY, run=run, detail="no STACK[...] lines"))
    else:
        out.append(chk("stack_free", PASS if mfree >= lim else FAIL, value=mfree, threshold=lim, run=run,
                       detail=f"per task min: {stacks}" if stacks else "", blocker=mfree < lim))
    if dig(s, "metrics.health.present"):
        hung = _m(s, "health.suspected_hangs") or []
        out.append(chk("hung_nodes", FAIL if hung else PASS, value=hung or 0, run=run, blocker=bool(hung),
                       detail="HEALTH heartbeat stopped before end of log (deadlock?)" if hung else ""))
        mh = _m(s, "health.minheap_min")
        lim_h = th.get("min_heap_bytes", 10000)
        if mh is not None:
            out.append(chk("min_free_heap", PASS if mh >= lim_h else GREY, value=mh, threshold=lim_h, run=run,
                           detail=f"worst heap slope {_m(s, 'health.worst_heap_slope_bytes_per_h')} B/h"))
    elif s.get("metrics") is not None:
        out.append(chk("hung_nodes", INCOMPLETE, run=run, detail="no HEALTH heartbeat lines"))

    if cell.startswith("reliable"):
        ack = _m(s, "reliable.ack_ratio")
        c = _need("reliable:ack_ratio", s, ack, run)
        if c:
            out.append(c)
        else:
            out.append(chk("reliable:ack_ratio", PASS if ack >= th.get("ack_ratio", 0.95) else FAIL,
                           value=round(ack, 4), threshold=th.get("ack_ratio", 0.95), run=run))
        rtt = _m(s, "reliable.rtt_ms_p95")
        if rtt is not None:
            lim_ms = th.get("rtt_p95_max_s", 900) * 1000
            out.append(chk("reliable:rtt_p95_ms", PASS if rtt <= lim_ms else GREY, value=rtt,
                           threshold=lim_ms, run=run,
                           detail=f"outliers excluded: {_m(s, 'reliable.rtt_outliers')} "
                                  "(unclamped echo_ts, known open item)"))
        nfail = _m(s, "reliable.n_fail")
        if nfail:
            out.append(chk("reliable:failures", GREY, value=nfail, run=run,
                           detail=f"reasons: {_m(s, 'reliable.fail_reasons')}"))
        nq = _m(s, "diag.reliable_not_queued")
        sends = _m(s, "reliable.n_tx")
        if nq is not None and sends:
            frac = nq / sends
            out.append(chk("reliable:not_queued_frac", PASS if frac <= th.get("not_queued_grey_frac", 0.05)
                           else GREY, value=round(frac, 4), threshold=th.get("not_queued_grey_frac"), run=run))
        fp = dig(s, "app.fair_pdr")
        if fp is not None:
            out.append(chk("reliable:app_fair_pdr", PASS, value=round(fp, 4), run=run, detail="informational"))
    elif cell.startswith("group"):
        rx = _m(s, "group.delivery_ratio_mean")
        c = _need("group:rx_ratio", s, rx, run)
        if c:
            out.append(c)
        else:
            st = PASS if rx >= th.get("group_rx", 0.9) else (GREY if rx >= th.get("group_rx_fail", 0.5) else FAIL)
            out.append(chk("group:rx_ratio", st, value=round(rx, 4), threshold=th.get("group_rx", 0.9), run=run))
        dup = _m(s, "group.duplicate_ratio")
        if dup is not None:
            out.append(chk("group:dup_ratio", PASS if dup <= th.get("group_dup", 0.01) else GREY,
                           value=round(dup, 4), threshold=th.get("group_dup", 0.01), run=run,
                           detail="receivers keep 32 delivery windows (known open item)"))
        acks = _m(s, "group.win_ack_completeness_mean")
        if acks is not None:
            out.append(chk("group:acks", PASS, value=acks, run=run,
                           detail="informational; library notes ~28% ACK completeness at 25 nodes"))
    elif cell.startswith("stopstart"):
        rj = _m(s, "stopstart.rejoin_superframes_max")
        cycles = _m(s, "stopstart.n_cycles")
        rejoined = _m(s, "stopstart.n_rejoined")
        if cycles and rejoined is not None and rejoined < cycles:
            out.append(chk("stopstart:rejoin", FAIL, value=f"{rejoined}/{cycles}", run=run, blocker=True,
                           detail="Start() did not lead to a rejoin in every cycle"))
        if _m(s, "stopstart.n_start_failed"):
            out.append(chk("stopstart:start_failed", FAIL, value=_m(s, "stopstart.n_start_failed"), run=run,
                           blocker=True, detail="Start() returned an error"))
        seqr = _m(s, "stopstart.seq_reset_cycles")
        if seqr is not None:
            out.append(chk("stopstart:seq_survives_restart", PASS if seqr == 0 else FAIL, value=seqr, run=run,
                           detail="2.0.0 claims the packet sequence counter survives Stop/Start"))
        c = _need("stopstart:rejoin_superframes", s, rj, run)
        if c:
            out.append(c)
        else:
            lim = th.get("rejoin_superframes", 3)
            out.append(chk("stopstart:rejoin_superframes", PASS if rj <= lim else GREY, value=rj,
                           threshold=lim, run=run, detail=f"cycles={cycles}" if cycles is not None else ""))
        if cycles == 0:
            out.append(chk("stopstart:cycles", FAIL, value=0, run=run, detail="no Stop/Start cycle observed"))
    elif is_failover:
        fired = sum(d["nm_failover"] for d in (s.get("scan") or {}).values())
        out.append(chk("nm_failover:fired", PASS if fired else FAIL, value=fired, run=run,
                       detail="" if fired else "no NM_FAILOVER line — failover never triggered"))
        tk = _m(s, "nm_failover.takeover_s_max")
        nf, nt = _m(s, "nm_failover.n_failovers"), _m(s, "nm_failover.n_taken_over")
        if fired and nf is not None and nt is not None and nt < nf:
            out.append(chk("nm_failover:taken_over", FAIL, value=f"{nt}/{nf}", run=run, blocker=True,
                           detail="no other node became NM after a failover"))
        if fired:
            c = _need("nm_failover:takeover_s", s, tk, run)
            if c:
                out.append(c)
            else:
                lim = th.get("nm_takeover_max_s", 900)
                out.append(chk("nm_failover:takeover_s", PASS if tk <= lim else FAIL, value=tk,
                               threshold=lim, run=run))
    elif cell.startswith("slot_overflow"):
        ds = dig(s, "metrics.data_slots") if dig(s, "metrics.data_slots.present") else None
        maxds = s["cfg"].get("max_ds")
        out += [c for c in check_join(s, remote, False, name="slot_overflow:join")]
        if ds is None:
            out.append(chk("slot_overflow:allocation", INCOMPLETE, run=run,
                           detail="no data_slots section from lm200_metrics"))
        else:
            zero = ds.get("nodes_zero_slots")
            out.append(chk("slot_overflow:allocation", PASS if zero else GREY, value=zero, run=run,
                           threshold=f"control index >= {maxds} → 0 slots",
                           detail="2.0.0 behaviour change vs 134ae25 partial allocation (documented)"))
    elif cell.startswith("highsf"):
        out += check_join(s, remote, require_remote=False, name="highsf:join")
        hist = _m(s, "join.backoff_hist")
        if hist is not None:
            out.append(chk("highsf:join_backoff_hist", PASS, value=hist, run=run, detail="informational"))
        nr = dig(s, "formation.join_retries_total")
        if nr is not None:
            out.append(chk("highsf:join_retries", PASS, value=nr, run=run, detail="informational"))
    elif cell.startswith("long_link"):
        ff = s.get("formation_full") or {}
        rj = sorted(set(ff.get("joined", [])) & remote)
        need = th.get("longlink_min_remote_joined", 3)
        out.append(chk("long_link:remote_joined", PASS if len(rj) >= need else FAIL, value=rj,
                       threshold=need, run=run))
        rt = sorted(set(ff.get("rt_complete", [])) & remote)
        out.append(chk("long_link:remote_routes", PASS if len(rt) >= need else GREY, value=rt,
                       threshold=need, run=run))
        flaps = dig(s, "formation.flaps") or {}
        f3428 = flaps.get("3428", 0)
        out.append(chk("long_link:3428_flaps", PASS if f3428 <= 2 else GREY, value=f3428, threshold=2,
                       run=run, detail="joins lost by the bridge node during the window"))
        per_flow = dig(s, "app.per_flow") or {}
        rem = {k: v for k, v in per_flow.items() if str(k)[-4:].upper() in remote}
        if rem:
            out.append(chk("long_link:remote_flows", PASS, value=rem, run=run, detail="informational"))
    else:
        out.append(chk("cell", INCOMPLETE, run=run, detail=f"no G2 rules for cell {cell!r}"))
    return [dict(c, cell=s.get("cell")) for c in out]


def g2(paths: list[Path], cfg: dict, cache_dir: Path) -> dict:
    checks = []
    runs = [r for p in paths for r in run_dirs(p)]
    if not runs:
        return gate_result("g2", [chk("runs", INCOMPLETE, detail=f"no run dirs under {paths}")])
    for r in runs:
        s = summarize_run(r, cache_dir, with_metrics=True)
        checks += _g2_cell(s, cfg)
    # Expected cells present?
    want = ["reliable_SF7", "reliable_SF9", "group", "stopstart", "nm_failover", "slot_overflow",
            "highsf_join", "long_link"]
    seen = {cell_of(r)[0] for r in runs if cell_of(r)}
    for w in want:
        if not any(c.startswith(w) for c in seen):
            checks.append(chk(f"cell_present:{w}", INCOMPLETE, detail="no valid run for this cell"))
    return gate_result("g2", checks)


# ── CLI ──────────────────────────────────────────────────────────────────────


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("gate", choices=["g_minus1", "g0", "g1", "g2"])
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--config", type=Path, default=None)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--expect-sha", default="__config__",
                    help="LoRaMesher sha prefix expected in build/upload logs "
                         "(default: lib_sha.new from campaign.yaml; '' disables)")
    ap.add_argument("--cache-dir", type=Path, default=None)
    ap.add_argument("--expect-radiolib", default="__config__",
                    help="RadioLib version every gateway must build (default: expected_radiolib "
                         "from campaign.yaml; '' disables, e.g. for July runs that predate the pin)")
    ap.add_argument("--radiolib-probe", type=Path, default=None,
                    help="per-gateway RadioLib snapshot (default: <campaign_dir>/radiolib.json)")
    a = ap.parse_args(argv)
    cfg = load_config(a.config)
    sha = dig(cfg, "lib_sha.new") if a.expect_sha == "__config__" else (a.expect_sha or None)
    cache = a.cache_dir or (tpath(cfg.get("campaign_dir", "runs/lm200_campaign")) / "cache")
    global RADIOLIB_PROBE, EXPECTED_RADIOLIB
    RADIOLIB_PROBE = a.radiolib_probe or (cache.parent / "radiolib.json")
    EXPECTED_RADIOLIB = (cfg.get("expected_radiolib") if a.expect_radiolib == "__config__"
                         else (a.expect_radiolib or None))
    if a.gate == "g_minus1":
        res = g_minus1([Path(p) for p in a.paths], cfg, sha)
    elif a.gate == "g0":
        res = g0([tpath(p) for p in a.paths], cfg, sha, cache)
    elif a.gate == "g1":
        pairs = {}
        for spec in a.paths:
            kind, _, rest = spec.partition("=")
            new, _, base = rest.partition(":")
            if not base:
                base = dig(cfg, f"baseline.lm200_{kind}")
            pairs[kind] = (tpath(new), tpath(base) if base else None)
        res = g1(pairs, cfg, sha, cache)
    else:
        res = g2([tpath(p) for p in a.paths], cfg, cache)
    res["inputs"] = a.paths
    text = json.dumps(res, indent=2, default=list)
    if a.out:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(text)
    counts = Counter(c["status"] for c in res["checks"])
    print(f"{a.gate}: {res['status']}  ({', '.join(f'{k}={v}' for k, v in sorted(counts.items()))})")
    for c in res["checks"]:
        if c["status"] != PASS:
            print(f"  [{c['status']}] {c['name']} {c.get('run', '')}: value={c.get('value')} "
                  f"threshold={c.get('threshold')} {c.get('detail', '')}"[:400])
    return {PASS: 0, GREY: 0, INCOMPLETE: 3, FAIL: 1}[res["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
