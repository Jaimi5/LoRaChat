#!/usr/bin/env python3
"""Unattended LoRaMesher 2.0.0 testbed campaign: one command, resumable.

    nohup python3 scripts/testbed/lm200/campaign.py > lm200_campaign.out 2>&1 &

Stages (lm200/campaign.yaml): preflight → prewarm → T0 (smoke, G0) → T1a (core
regression, G1) → [T1b interleaved A/B if G1 is GREY] → T2 (features, G2) → report.
Other configs (lm200/campaign_sleep.yaml) list their own stages: each may pick a
firmware `env:` (a key of `envs:`) and a `tag:` (batch dirs become <batch>__<tag>),
run several gates (g0/g1/g2/ab/sleep), be `optional:` (skipped when the remaining
`budget_h` cannot fit its estimate), and with `stop_on_fail_scope: env` a failing
stop_on_fail gate skips only that env's later stages.

* State lives in <campaign_dir>/state.json; re-running the same command resumes
  (completed stages/batches are skipped). Everything is logged to campaign.log.
* It never pushes or merges git, never edits testbed.conf, and never touches the
  July `runs/*__lmv1|__lmv2` dirs. Invalid runs are moved aside (*.FAILED-<ts>),
  never deleted, and the cell is re-run (cell_attempts).
* G0 FAIL stops the campaign (straight to the report); G1/G2 FAIL is recorded and
  the campaign continues so the report is complete.

Flags: --dry-run (render only: run_batch --dry-run into throw-away `lm200dry_*`
batch dirs, no ssh, no git fetch; gates run on --fixture-runs; prints the stage
plan with env / tag / expected sha / estimated wall-clock), --from-stage,
--only-stage, --config, --reset-state.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
TESTBED = HERE.parent
REPO = TESTBED.parents[1]
DEPLOY = TESTBED / "deploy.sh"
RUN_BATCH = TESTBED / "runner" / "run_batch.py"
sys.path.insert(0, str(TESTBED))
sys.path.insert(0, str(HERE))

import yaml  # noqa: E402

import gates  # noqa: E402
from runner import run_checks  # noqa: E402
from runner.batch import load_batch  # noqa: E402

LEGACY_STAGES = ["T0", "T1a", "T1b", "T2"]
DRY = "lm200dry_"
# G1 kind per batch name (tag stripped); campaign configs may extend it (g1_kinds).
G1_KINDS = {"lm200_formation": "formation", "lm200_13node": "13node",
            "lm200_reach16": "reach16", "lm200_dataslots": "dataslots"}


def stage_order(cfg: dict) -> list[str]:
    names = [s["name"] for s in cfg.get("stages", [])]
    return ["preflight", "prewarm"] + names + ["report"]


def is_placeholder(sha) -> bool:
    return isinstance(sha, str) and (sha.upper().startswith("TODO") or "<" in sha)


def now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")


class Campaign:
    def __init__(self, args):
        self.args = args
        if args.config is not None:
            # Subprocesses run from the repo root: hand them an absolute path.
            c = Path(args.config)
            args.config = (c if c.is_absolute() else
                           (Path.cwd() / c if (Path.cwd() / c).is_file() else TESTBED / c)).resolve()
        self.cfg = gates.load_config(args.config)
        self.dir = gates.tpath(self.cfg.get("campaign_dir", "runs/lm200_campaign"))
        if args.dry_run:
            self.dir = self.dir.with_name(self.dir.name + "_dryrun")
        self.dir.mkdir(parents=True, exist_ok=True)
        (self.dir / "gates").mkdir(exist_ok=True)
        (self.dir / "tmp").mkdir(exist_ok=True)
        self.cache = self.dir / "cache"
        self.state_path = self.dir / "state.json"
        if args.reset_state and self.state_path.exists():
            self.state_path.rename(self.dir / f"state.{stamp()}.json")
        self.state = self._load_state()
        self.logf = open(self.dir / "campaign.log", "a", buffering=1)
        self.testbed_conf = self._read_testbed_conf()
        self.order = stage_order(self.cfg)
        for opt in (args.from_stage, *(args.only_stage or [])):
            if opt and opt not in self.order:
                raise SystemExit(f"unknown stage {opt!r}; stages: {self.order}")
        self.envs = dict(self.cfg.get("envs") or {"new": "ttgo-t-beam-v2"})
        self.default_env = self.cfg.get("default_env") or ("new" if "new" in self.envs else next(iter(self.envs)))
        self.config_h = None   # committed src/config.h text, read at preflight (CFG check)

    # ── bookkeeping ──────────────────────────────────────────────────────────
    def _load_state(self) -> dict:
        if self.state_path.is_file():
            return json.loads(self.state_path.read_text())
        return {"created": now(), "stages": {}, "batches": {}, "gates": {}, "incomplete": [],
                "failed_runs": [], "events": [], "status": "running"}

    def save(self):
        tmp = self.state_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.state, indent=2, default=list))
        tmp.replace(self.state_path)

    def log(self, msg: str):
        line = f"{now()}  {msg}"
        print(line, flush=True)
        self.logf.write(line + "\n")

    def event(self, kind: str, **kw):
        self.state["events"].append({"ts": now(), "kind": kind, **kw})
        self.save()

    def _read_testbed_conf(self) -> dict:
        out = {"GIT_BRANCH": "new_loramesher", "REPO_PATH": "/home/lora/LoRaChat"}
        conf = TESTBED / "testbed.conf"
        if conf.is_file():
            for line in conf.read_text().splitlines():
                m = re.match(r'^\s*(GIT_BRANCH|REPO_PATH)\s*=\s*"?([^"#\s]+)"?', line)
                if m:
                    out[m.group(1)] = m.group(2)
        gws = []
        if conf.is_file():
            text = conf.read_text()
            m = re.search(r"declare -A GW_SSH=\((.*?)\n\)", text, re.S)
            if m:
                gws = sorted(set(re.findall(r"\[(GW-\d+)\]", m.group(1))))
        out["gateways"] = gws
        return out

    def sh(self, cmd: list[str], log_name: str | None = None, env: dict | None = None,
           timeout: int | None = None) -> int:
        """Run a command, tee its output into <campaign_dir>/logs/<log_name>."""
        self.log("$ " + " ".join(shlex.quote(c) for c in cmd))
        if self.args.dry_run and cmd and cmd[0] == "bash" and str(DEPLOY) in cmd:
            self.log("  (dry-run: deploy.sh not executed)")
            return 0
        logdir = self.dir / "logs"
        logdir.mkdir(exist_ok=True)
        lp = logdir / (log_name or f"cmd-{stamp()}.log")
        e = dict(os.environ, **(env or {}))
        with open(lp, "a") as fh:
            fh.write(f"\n===== {now()} $ {' '.join(cmd)}\n")
            fh.flush()
            try:
                p = subprocess.run(cmd, cwd=REPO, stdout=fh, stderr=subprocess.STDOUT, env=e,
                                   timeout=timeout)
                rc = p.returncode
            except subprocess.TimeoutExpired:
                rc = 124
                fh.write("\n[campaign] TIMEOUT\n")
        self.log(f"  rc={rc}  (log: {lp.relative_to(TESTBED) if lp.is_relative_to(TESTBED) else lp})")
        return rc

    def deploy(self, *args, log_name=None, timeout=None, env=None) -> int:
        return self.sh(["bash", str(DEPLOY), *args], log_name=log_name, timeout=timeout, env=env)

    def notify(self, status: str, summary: str):
        tpl = self.cfg.get("notify") or ""
        if not tpl or self.args.dry_run:
            return
        report = self.dir / "report" / "report.md"
        cmd = tpl.format(status=shlex.quote(status), summary=shlex.quote(summary),
                         report=shlex.quote(str(report)))
        try:
            subprocess.run(cmd, shell=True, timeout=60)
        except Exception as e:  # noqa: BLE001
            self.log(f"notify failed: {e}")

    def stage_done(self, name: str) -> bool:
        return self.state["stages"].get(name, {}).get("status") in ("done", "skipped")

    def mark(self, name: str, status: str, **kw):
        self.state["stages"][name] = {"status": status, "ts": now(), **kw}
        self.save()

    def stage_cfg(self, name: str) -> dict:
        for s in self.cfg.get("stages", []):
            if s["name"] == name:
                return s
        return {}

    # ── stages ───────────────────────────────────────────────────────────────
    def preflight(self) -> bool:
        branch = self.testbed_conf["GIT_BRANCH"]
        self.log(f"preflight: gateways sync to origin/{branch}")
        if branch == "new_loramesher":
            self.log("WARNING: testbed.conf GIT_BRANCH is still new_loramesher (plan: lm200-eval)")
        # A stage whose firmware sha is still a placeholder cannot be validated.
        missing = sorted({self.stage_env(sc) for sc in self.cfg.get("stages", [])
                          if is_placeholder(self.sha_for(self.stage_env(sc)))})
        if missing:
            msg = f"lib_sha placeholder for env(s) {missing} — fill in lib_sha in {self.args.config}"
            if not self.args.dry_run:
                self.log("FATAL: " + msg)
                return False
            self.log("WARNING (dry-run): " + msg)
        self.config_h = run_checks.committed_config_h(REPO)
        # Local HEAD must be what the gateways will check out: the campaign never pushes.
        if not self.args.dry_run:
            subprocess.run(["git", "fetch", "-q", "origin", branch], cwd=REPO)
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True,
                              text=True).stdout.strip()
        remote = subprocess.run(["git", "rev-parse", f"origin/{branch}"], cwd=REPO,
                                capture_output=True, text=True).stdout.strip()
        self.state["lorachat_head"] = head
        if head != remote and not self.args.dry_run:
            self.log(f"FATAL: local HEAD {head[:9]} != origin/{branch} {remote[:9] or '?'} — "
                     f"push first (git push origin {branch}); the campaign never pushes.")
            return False
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=REPO,
                               capture_output=True, text=True).stdout.strip()
        if dirty:
            self.log(f"note: local tracked changes are NOT on the gateways:\n{dirty}")
        # Gateways reachable (retry with backoff up to status_wait_min).
        deadline = time.time() + 60 * gates.dig(self.cfg, "retries.status_wait_min", 30)
        delay = 30
        while True:
            if self.deploy("status", log_name="status.log", timeout=300) == 0:
                break
            if time.time() > deadline:
                self.log("FATAL: not all gateways reachable")
                return False
            self.log(f"gateways not all reachable — retry in {delay}s")
            time.sleep(delay)
            delay = min(delay * 2, 300)
        # Upgrade (git reset --hard origin/<branch> + pio pkg update) and verify HEAD.
        for attempt in range(1, gates.dig(self.cfg, "retries.upgrade_attempts", 3) + 1):
            if self.deploy("upgrade", log_name="upgrade.log", timeout=1800) == 0:
                break
            self.log(f"upgrade failed (attempt {attempt})")
            time.sleep(60)
        else:
            return False
        rp = self.testbed_conf["REPO_PATH"]
        if self.deploy("run-remote", f"cd {rp} && test \"$(git rev-parse HEAD)\" = {head}",
                       log_name="verify-head.log", timeout=300) != 0 and not self.args.dry_run:
            self.log(f"FATAL: not every gateway is at {head[:9]}")
            return False
        return True

    def prewarm(self, env: str) -> bool:
        """Build (no upload) the env on every gateway so the first timed upload
        is warm. Retries, then wipes that gateway's LoRaMesher libdeps once."""
        rp = self.testbed_conf["REPO_PATH"]
        gws = self.testbed_conf["gateways"] or [None]
        tries = gates.dig(self.cfg, "retries.prewarm_attempts", 3)
        tries_wipe = gates.dig(self.cfg, "retries.prewarm_after_wipe_attempts", 2)
        build = f"cd {rp} && $PIO_NICE pio run -e {env}"
        failed = []
        # First a parallel attempt on all gateways, then per-gateway retries.
        session = f"lm200-prewarm-{env}-{stamp()}"
        if self.deploy("run-remote", "-n", session, build, log_name=f"prewarm-{env}.log",
                       timeout=4 * 3600) == 0:
            self.probe_radiolib(env, f"prewarm-{env}")
            return True
        for gw in gws:
            if gw is None:
                continue
            ok = False
            for attempt in range(1, tries + tries_wipe + 1):
                if attempt == tries + 1:
                    self.log(f"{gw}: wiping .pio/libdeps/{env}/LoRaMesher after {tries} failed builds")
                    self.deploy("run-remote", "-g", gw, f"rm -rf {rp}/.pio/libdeps/{env}/LoRaMesher",
                                log_name=f"prewarm-{env}.log", timeout=600)
                if self.deploy("run-remote", "-g", gw, "-n", session, build,
                               log_name=f"prewarm-{env}.log", timeout=4 * 3600) == 0:
                    ok = True
                    break
                self.log(f"{gw}: prewarm attempt {attempt} failed")
            if not ok:
                failed.append(gw)
        self.probe_radiolib(env, f"prewarm-{env}")
        if failed:
            self.log(f"FATAL: prewarm failed on {failed} — every run needs every gateway")
            self.state["prewarm_failed"] = failed
            self.save()
            return False
        return True

    def probe_radiolib(self, env: str, tag: str):
        """Record (never change) the RadioLib each gateway has in .pio/libdeps/<env>.

        Unpinned, RadioLib is a transitive dependency that pio's upload-time graph does
        not print, and ^7.1.2 lets every gateway keep whatever it cached (7.7.1 on the
        testbed in July vs 7.8.1 on a fresh install). Written to state.json and to
        <campaign_dir>/radiolib.json, which gates.py uses when upload logs lack it."""
        if self.args.dry_run:
            return
        rp = self.testbed_conf["REPO_PATH"]
        session = f"lm200-radiolib-{stamp()}"
        self.deploy("run-remote", "-n", session,
                    f"grep -m1 '\"version\"' {rp}/.pio/libdeps/{env}/RadioLib/library.json "
                    f"|| echo 'RadioLib missing'", log_name="radiolib.log", timeout=300)
        d = REPO / "logs_testbed" / session
        per = {}
        for p in sorted(d.glob("GW-*-remote.log")) if d.is_dir() else []:
            m = re.findall(r'"version"\s*:\s*"([^"]+)"', p.read_text(errors="replace"))
            per[p.name.split("-remote")[0]] = sorted(set(m)) or ["?"]
        if not per:
            self.log("RadioLib probe returned nothing")
            return
        self.state.setdefault("radiolib", {})[tag] = per
        if tag.startswith("prewarm"):
            self.state.setdefault("radiolib_prewarm", {}).update(per)
        self.save()
        rl_path = self.dir / "radiolib.json"
        hist = json.loads(rl_path.read_text()) if rl_path.is_file() else {"history": []}
        hist["latest"] = per
        hist["history"].append({"ts": now(), "tag": tag, "env": env, "per_gateway": per})
        rl_path.write_text(json.dumps(hist, indent=2))
        uniq = sorted({v for vs in per.values() for v in vs})
        self.log(f"RadioLib per gateway ({tag}): {per}" + ("  <- DIFFERENT VERSIONS" if len(uniq) > 1 else ""))

    # ── batches ──────────────────────────────────────────────────────────────
    def _batch_yaml_for_run(self, yaml_path: Path, cells: list[str] | None,
                            batch_name: str | None = None) -> Path:
        """Temp copy with absolute base (and, for retries, only `cells`; the
        tagged batch name; for dry-run, a throw-away `lm200dry_` batch name)."""
        raw = yaml.safe_load(yaml_path.read_text())
        base = Path(raw["base"])
        raw["base"] = str(base if base.is_absolute() else (yaml_path.parent / base).resolve())
        if cells is not None:
            raw["cells"] = [c for c in raw["cells"] if str(c["id"]) in cells]
            raw["warmup_run"] = raw.get("warmup_run", False)
        if "warmup_run" in self.cfg:          # campaign-wide override (sleep campaign: false)
            raw["warmup_run"] = bool(self.cfg["warmup_run"])
        if batch_name:
            raw["batch"] = batch_name
        if self.args.dry_run:
            raw["batch"] = DRY + raw.get("batch", yaml_path.stem)
        out = self.dir / "tmp" / f"{yaml_path.stem}{'-retry' if cells else ''}-{stamp()}.yaml"
        out.write_text(yaml.safe_dump(raw, sort_keys=False))
        return out

    def _validate_run(self, run_dir: Path, first_of_cell: bool, sha: str | None = None) -> tuple[bool, str]:
        """A run is valid when every board was flashed (if this run uploaded),
        every board produced a log with a boot banner, the expected LoRaMesher
        was built, the host did not sleep, the window was not stretched, the
        serial monitors did not all stall, and every board's CFG line matches
        config.yaml (runner/run_checks.py)."""
        if not (run_dir / "logs").is_dir():
            return False, "no logs collected"
        lc = run_dir / "lifecycle.json"
        if lc.is_file():
            phases = json.loads(lc.read_text())
            critical = {"clock-check-t0", "upload", "post-upload-sync-reset", "push-config", "reset",
                        "monitor-start", "collect-logs"}
            bad = [p["name"] for p in phases if p.get("ok") is False and p["name"] in critical]
            if bad:
                return False, f"lifecycle phase(s) failed: {bad}"
        cfg = gates.run_config(run_dir)
        up = gates.scan_upload_logs(run_dir)
        if first_of_cell and up is None:
            return False, "first run of cell has no upload logs"
        if up is not None:
            not_ok = sorted(set(cfg["devices"]) - set(up["ok"]))
            if up["failed"] or not_ok:
                return False, f"upload failed on {up['failed'] or not_ok} (chip errors {up['chip_errors']})"
            sha = sha if sha is not None else self.expected_sha
            if sha and any(not v.split("+sha.")[-1].startswith(sha) for v in up["lib_versions"]):
                return False, f"wrong LoRaMesher built: {up['lib_versions']} (want {sha})"
            exp_rl = (self.args.expect_radiolib if self.args.expect_radiolib is not None
                      else self.cfg.get("expected_radiolib"))
            probs = gates.radiolib_problems(up, exp_rl or None)
            if probs:
                return False, "RadioLib: " + "; ".join(probs)
        n, missing = gates._check_reset(run_dir)
        absent = sorted(set(cfg["devices"]) - {gates._short_id(p) for p in (run_dir / "logs").glob("monitor-dev-*")})
        if absent or missing:
            return False, f"no log={absent} no boot banner={missing}"
        res = run_checks.check_run(run_dir, self.config_h)
        why = run_checks.invalid_reason(res)
        if why:
            return False, why
        skipped = [f"{r['check']}: {r['detail']}" for r in res if r["ok"] is None]
        return True, "ok" + (f" ({'; '.join(skipped)})" if skipped else "")

    def _validate_warmup_upload(self, run_dir: Path, sha: str | None = None) -> tuple[bool, str]:
        """The warmup rep flashes every board for the cell; its reps are only valid
        if that upload succeeded with the expected LoRaMesher and RadioLib."""
        up = gates.scan_upload_logs(run_dir)
        if up is None:
            return False, "warmup run has no upload logs"
        cfg = gates.run_config(run_dir)
        not_ok = sorted(set(cfg["devices"]) - set(up["ok"]))
        if up["failed"] or not_ok:
            return False, f"upload failed on {up['failed'] or not_ok}"
        sha = sha if sha is not None else self.expected_sha
        if sha and any(not v.split("+sha.")[-1].startswith(sha) for v in up["lib_versions"]):
            return False, f"wrong LoRaMesher built: {up['lib_versions']} (want {sha})"
        exp_rl = (self.args.expect_radiolib if self.args.expect_radiolib is not None
                  else self.cfg.get("expected_radiolib"))
        probs = gates.radiolib_problems(up, exp_rl or None)
        return (False, "RadioLib: " + "; ".join(probs)) if probs else (True, "ok")

    expected_sha = None

    # ── envs / tags / estimates ──────────────────────────────────────────────
    def stage_env(self, sc: dict) -> str:
        return sc.get("env") or self.default_env

    def stage_tag(self, sc: dict) -> str | None:
        if "tag" in sc:
            return sc["tag"]
        if "env" in sc:
            return (self.cfg.get("tags") or {}).get(sc["env"], sc["env"])
        return None

    def sha_for(self, env_key: str):
        if self.args.expect_sha is not None:
            return self.args.expect_sha or None
        return gates.dig(self.cfg, f"lib_sha.{env_key}")

    @staticmethod
    def tagged(name: str, tag: str | None) -> str:
        return f"{name}__{tag}" if tag else name

    def stage_batches(self, sc: dict) -> list[dict]:
        """Normalised batch entries: legacy [yaml, extra] or {yaml, extra, env, tag}."""
        out = []
        for b in sc.get("batches", []):
            if isinstance(b, dict):
                ent = {"yaml": b["yaml"], "extra": list(b.get("extra", [])),
                       "env": b.get("env") or self.stage_env(sc),
                       "tag": b["tag"] if "tag" in b else self.stage_tag(sc),
                       "explicit_env": "env" in b or "env" in sc}
            else:
                ent = {"yaml": b[0], "extra": list(b[1]), "env": self.stage_env(sc), "tag": self.stage_tag(sc),
                       "explicit_env": "env" in sc}
            ent["name"] = self.tagged(load_batch(gates.tpath(ent["yaml"])).name, ent["tag"])
            ent["dir"] = TESTBED / "runs" / ((DRY if self.args.dry_run else "") + ent["name"])
            out.append(ent)
        return out

    def batch_key(self, ent: dict) -> str:
        key = f"{ent['yaml']} {' '.join(ent['extra'])}".strip()
        return f"{key} @{ent['tag']}" if ent.get("tag") else key

    def estimate_batch_min(self, ent: dict) -> float:
        """Wall-clock estimate: per cell (warmup + duration + cooldown) x runs, plus an
        upload for the first run and fixed per-run overhead (reset, monitors, logs)."""
        batch = load_batch(gates.tpath(ent["yaml"]))
        runs_n = batch.runs
        if "--runs" in ent["extra"]:
            runs_n = int(ent["extra"][ent["extra"].index("--runs") + 1])
        ov = self.cfg.get("overhead") or {}
        up, per_run = ov.get("upload_min", 20), ov.get("per_run_min", 3)
        tot = 0.0
        for c in batch.cells:
            w = c.warmup_min if c.warmup_min is not None else batch.warmup_min
            d = c.duration_min if c.duration_min is not None else batch.duration_min
            k = c.cooldown_min if c.cooldown_min is not None else batch.cooldown_min
            tot += (w + d + k + per_run) * runs_n + up
        return tot

    def estimate_stage_min(self, sc: dict, prewarmed: set) -> float:
        ov = self.cfg.get("overhead") or {}
        est = sum(self.estimate_batch_min(e) for e in self.stage_batches(sc))
        if self.stage_env(sc) not in prewarmed and sc.get("batches"):
            est += ov.get("prewarm_min", 30)
        return est + ov.get("gate_min", 5) * len(self.stage_gates(sc))

    @staticmethod
    def stage_gates(sc: dict) -> list[dict]:
        """Gate specs: `gate: g0` / `gates: [g0, {gate: ab, with_stages: [S1]}]`."""
        raw = sc.get("gates") or ([sc["gate"]] if sc.get("gate") else [])
        return [g if isinstance(g, dict) else {"gate": g} for g in raw]

    def gate_id(self, gate: str, stage: str) -> str:
        return f"{gate}-{stage}" if self.cfg.get("gate_ids_per_stage") else gate

    def plan(self) -> list[dict]:
        """Stage table (env, tag, sha, batch dirs, estimate) for logs and dry runs."""
        rows, cum, prewarmed = [], 0.0, set()
        if not self.stage_done("prewarm"):
            for e in self.cfg.get("prewarm_envs") or [self.default_env]:
                cum += (self.cfg.get("overhead") or {}).get("prewarm_min", 30)
                prewarmed.add(e)
        budget = self.cfg.get("budget_h")
        for sc in self.cfg.get("stages", []):
            env = self.stage_env(sc)
            est = self.estimate_stage_min(sc, prewarmed) if sc.get("conditional") is None else 0.0
            skip = bool(sc.get("optional") and budget and cum / 60 > budget - est / 60)
            if not skip:
                cum += est
                if sc.get("batches"):
                    prewarmed.add(env)
            ents = self.stage_batches(sc)
            rows.append({"stage": sc["name"], "env": env, "pio_env": self.envs.get(env),
                         "tag": self.stage_tag(sc), "sha": self.sha_for(env),
                         "batches": [f"{e['name']} ({Path(e['yaml']).stem}"
                                     f"{' ' + ' '.join(e['extra']) if e['extra'] else ''})" for e in ents],
                         "gates": [g["gate"] for g in self.stage_gates(sc)],
                         "est_min": round(est), "cum_h": round(cum / 60, 1),
                         "optional": bool(sc.get("optional")), "budget_skip": skip,
                         "conditional": sc.get("conditional")})
        return rows

    def log_plan(self):
        rows = self.plan()
        self.log("plan: stage | env (pio env) | tag | lib sha | batches | gates | est min | cum h")
        for r in rows:
            flag = " OPTIONAL" if r["optional"] else ""
            flag += " (skipped: over budget)" if r["budget_skip"] else ""
            flag += f" (only if {r['conditional']})" if r["conditional"] else ""
            self.log(f"  {r['stage']:<6} {r['env']} ({r['pio_env']}) | {r['tag'] or '-'} | {r['sha'] or '-'} | "
                     f"{', '.join(r['batches']) or '-'} | {','.join(r['gates']) or '-'} | "
                     f"{r['est_min']} | {r['cum_h']}{flag}")
        if self.cfg.get("budget_h"):
            self.log(f"  budget_h {self.cfg['budget_h']}: estimated total "
                     f"{max((r['cum_h'] for r in rows), default=0)} h")
        return rows

    def run_batch(self, yaml_rel: str, extra: list[str], env_name: str | None = None,
                  tag: str | None = None, sha: str | None = None) -> list[Path]:
        """Run one batch YAML with validation + per-cell retries. Returns valid run dirs.
        `env_name` is the PlatformIO env (DEFAULT_ENV for deploy.sh), `tag` makes the
        batch dir <batch>__<tag>, `sha` the LoRaMesher sha its uploads must show."""
        key = f"{yaml_rel} {' '.join(extra)}".strip()
        if tag:
            key += f" @{tag}"
        st = self.state["batches"].get(key, {})
        if st.get("status") == "done":
            self.log(f"skip (done): {key}")
            return [Path(p) for p in st.get("valid", [])]
        yaml_path = gates.tpath(yaml_rel)
        batch = load_batch(yaml_path)
        bname = self.tagged(batch.name, tag)
        name = (DRY if self.args.dry_run else "") + bname
        broot = TESTBED / "runs" / name
        env = {"DEFAULT_ENV": env_name} if env_name else None
        runs_n = batch.runs
        if "--runs" in extra:
            runs_n = int(extra[extra.index("--runs") + 1])
        attempts = gates.dig(self.cfg, "retries.cell_attempts", 3)
        pending = [c.id for c in batch.cells]
        valid: list[Path] = []
        for attempt in range(1, attempts + 1):
            if not pending:
                break
            before = {p.name for p in broot.iterdir()} if broot.is_dir() else set()
            tmp_yaml = self._batch_yaml_for_run(yaml_path, None if attempt == 1 else pending,
                                                batch_name=bname if tag else None)
            args = [sys.executable, str(RUN_BATCH), str(tmp_yaml), *extra]
            # Upgrade before every invocation: `git reset --hard` restores the committed
            # src/config.h so no sed-edited value from a previous batch leaks in.
            if attempt > 1 or self.args.dry_run:
                args.append("--skip-upgrade")
            if self.args.dry_run:
                args += ["--dry-run", "--skip-clock-check"]
            self.log(f"batch {key} attempt {attempt}: cells {pending}"
                     + (f" (env {env_name})" if env_name else ""))
            # Watchdog: a wedged run_batch must not hang an unattended campaign forever.
            est = sum(((c.warmup_min if c.warmup_min is not None else batch.warmup_min)
                       + (c.duration_min if c.duration_min is not None else batch.duration_min)
                       + (c.cooldown_min if c.cooldown_min is not None else batch.cooldown_min))
                      * (runs_n + (1 if batch.warmup_run else 0)) + 45
                      for c in batch.cells)
            rc = self.sh(args, log_name=f"batch-{bname}.log", env=env,
                         timeout=None if self.args.dry_run else int((2 * est + 120) * 60))
            if rc != 0:
                self.log(f"  run_batch rc={rc} (per-run validation decides what to retry)")
            new = sorted(p for p in broot.iterdir() if p.name not in before and p.is_dir()) \
                if broot.is_dir() else []
            if self.args.dry_run:
                for p in new:
                    self.log(f"  dry-run rendered {p.relative_to(TESTBED)}")
                return new
            retry = set()
            for p in new:
                c = gates.cell_of(p)
                if c is None:
                    continue
                cell, rep = c
                if rep == 0:  # warmup (flash + boot-verify) run: only its upload matters
                    ok, why = self._validate_warmup_upload(p, sha)
                    if not ok:
                        retry.add(cell)
                        self.state["failed_runs"].append({"run": str(p), "why": why, "ts": now()})
                        self.log(f"  INVALID warmup: {p.name}: {why}")
                    continue
                # With a warmup run, r00 does the upload and r01 only push-config + reset.
                first = (rep == 1 and not batch.warmup_run)
                ok, why = self._validate_run(p, first, sha)
                if ok:
                    valid.append(p)
                    self.log(f"  valid: {p.name}" + (f" — {why[3:]}" if why != "ok" else ""))
                else:
                    dst = p.with_name(p.name + f".FAILED-{stamp()}")
                    p.rename(dst)
                    retry.add(cell)
                    self.state["failed_runs"].append({"run": str(dst), "why": why, "ts": now()})
                    self.log(f"  INVALID: {p.name}: {why} → {dst.name}")
            # Cells with fewer valid reps than requested are retried (whole cell).
            got = {}
            for p in valid:
                c = gates.cell_of(p)
                got[c[0]] = got.get(c[0], 0) + 1
            pending = [cid for cid in pending if got.get(cid, 0) < runs_n or cid in retry]
            if pending:
                # Clock skew is the one run_batch rejection we can fix from here.
                self.deploy("sync-time", log_name="sync-time.log", timeout=600)
                if attempt < attempts:
                    # Drop partial reps of cells being retried so reps stay consistent.
                    for p in list(valid):
                        c = gates.cell_of(p)
                        if c[0] in pending:
                            valid.remove(p)
                            dst = p.with_name(p.name + f".FAILED-{stamp()}")
                            p.rename(dst)
                            self.state["failed_runs"].append(
                                {"run": str(dst), "why": "cell re-run (incomplete reps)", "ts": now()})
        self.probe_radiolib(env_name or gates.dig(self.cfg, "envs.new", "ttgo-t-beam-v2"),
                            f"after-{bname}-{stamp()}")
        for cid in pending:
            self.state["incomplete"].append({"batch": bname, "cell": cid, "ts": now()})
            self.log(f"  INCOMPLETE: {bname}/{cid} after {attempts} attempt(s)")
        self.state["batches"][key] = {"status": "done", "valid": [str(p) for p in valid],
                                      "incomplete": pending, "ts": now()}
        self.save()
        return valid

    # ── gates ────────────────────────────────────────────────────────────────
    def run_gate(self, gate: str, args: list[str], gate_id: str | None = None,
                 env_key: str | None = None) -> dict:
        gid = gate_id or gate
        out = self.dir / "gates" / f"{gid}.json"
        cmd = [sys.executable, str(HERE / "gates.py"), gate, *args, "--out", str(out),
               "--cache-dir", str(self.cache)]
        if self.args.config:
            cmd += ["--config", str(self.args.config)]
        if gate in ("g0", "g1"):
            sha = self.sha_for(env_key) if env_key else self.args.expect_sha
            if sha is not None and not is_placeholder(sha):
                cmd += ["--expect-sha", sha]
            elif env_key and is_placeholder(sha):
                cmd += ["--expect-sha", ""]
        if gate == "g0" and env_key:
            cmd += ["--expect-env", self.envs.get(env_key, "")]
        if self.args.expect_radiolib is not None:
            cmd += ["--expect-radiolib", self.args.expect_radiolib]
        self.sh(cmd, log_name=f"gate-{gid}.log")
        try:
            res = json.loads(out.read_text())
        except Exception as e:  # noqa: BLE001
            res = {"gate": gate, "status": "INCOMPLETE", "checks": [], "error": str(e)}
        self.state["gates"][gid] = {"status": res.get("status"), "ab_trigger": res.get("ab_trigger"),
                                    "gate": gate, "env": env_key, "file": str(out), "ts": now()}
        self.save()
        self.log(f"GATE {gid}: {res.get('status')}")
        return res

    def _fixture(self, kind: str) -> list[str] | None:
        """--dry-run: gates run against existing runs given as --fixture-runs KIND=DIR."""
        out = []
        for spec in self.args.fixture_runs or []:
            k, _, v = spec.partition("=")
            if k == kind:
                out.append(v)
        return out or None

    # ── main loop ────────────────────────────────────────────────────────────
    def selected(self, name: str) -> bool:
        if self.args.only_stage:
            return name in self.args.only_stage
        if self.args.from_stage:
            return self.order.index(name) >= self.order.index(self.args.from_stage)
        return True

    def ensure_prewarmed(self, env_key: str) -> bool:
        done = self.state.setdefault("prewarmed", [])
        if env_key in done or (env_key == self.default_env and self.stage_done("prewarm")
                               and "prewarm_envs" not in self.cfg):
            return True
        pio = self.envs.get(env_key)
        if not pio:
            self.log(f"FATAL: env {env_key!r} not in envs {self.envs}")
            return False
        self.log(f"--- prewarm {env_key} ({pio})")
        if self.args.dry_run or self.prewarm(pio):
            done.append(env_key)
            self.save()
            return True
        return False

    def run(self) -> int:
        self.expected_sha = self.sha_for(self.default_env)
        self.log(f"=== lm200 campaign {'(DRY RUN) ' if self.args.dry_run else ''}— state {self.state_path}")
        plan = {r["stage"]: r for r in self.log_plan()}
        stop = False
        env_stopped = self.state.setdefault("env_stopped", [])
        scope = self.cfg.get("stop_on_fail_scope", "campaign")

        if self.selected("preflight") and not self.stage_done("preflight"):
            if not self.preflight():
                self.mark("preflight", "failed")
                return self.finish("NO-GO", "preflight failed (unpushed code / gateways / upgrade / sha)")
            self.mark("preflight", "done")
        elif self.config_h is None:
            self.config_h = run_checks.committed_config_h(REPO)
        if self.selected("prewarm") and not self.stage_done("prewarm"):
            for env_key in self.cfg.get("prewarm_envs") or [self.default_env]:
                if not self.ensure_prewarmed(env_key):
                    self.mark("prewarm", "failed")
                    return self.finish("NO-GO", f"prewarm failed on {self.state.get('prewarm_failed')}")
            self.mark("prewarm", "done")

        budget_h = self.cfg.get("budget_h")
        for sc in self.cfg.get("stages", []):
            name = sc["name"]
            if stop or not self.selected(name):
                continue
            if self.stage_done(name):
                self.log(f"skip stage (done): {name}")
                continue
            env_key = self.stage_env(sc)
            if env_key in env_stopped:
                self.mark(name, "skipped", reason=f"an earlier stop_on_fail gate failed for env {env_key}")
                self.log(f"skip stage {name}: env {env_key} stopped")
                continue
            if sc.get("conditional") == "g1_grey":
                g1 = self.state["gates"].get("g1") or {}
                if not (g1.get("ab_trigger") and self.cfg.get("auto_ab", True)):
                    self.mark(name, "skipped", reason="G1 not GREY or auto_ab off")
                    continue
                self.log(f"--- stage {name}: {sc.get('title')} (G1 was GREY)")
                self.mark(name, "running")
                ok = self.run_ab(sc["ab"])
                self.mark(name, "done" if ok else "failed")
                continue
            est = self.estimate_stage_min(sc, set(self.state.get("prewarmed", [])))
            if sc.get("optional") and budget_h:
                # Dry run: the plan's estimated clock; otherwise wall time since the campaign began.
                elapsed_h = (plan[name]["cum_h"] - (0 if plan[name]["budget_skip"] else est / 60)
                             if self.args.dry_run else
                             (datetime.now(timezone.utc) - datetime.strptime(
                                 self.state["created"], "%Y-%m-%dT%H:%M:%SZ").replace(
                                 tzinfo=timezone.utc)).total_seconds() / 3600)
                if elapsed_h > budget_h - est / 60:
                    self.mark(name, "skipped", reason=f"budget: {elapsed_h:.1f} h elapsed, "
                              f"stage needs ~{est / 60:.1f} h of {budget_h} h")
                    self.log(f"skip optional stage {name}: over budget ({elapsed_h:.1f} h + {est / 60:.1f} h "
                             f"> {budget_h} h)")
                    continue
            self.log(f"--- stage {name}: {sc.get('title')}"
                     + (f" [env {env_key} = {self.envs.get(env_key)}, tag {self.stage_tag(sc)}]"
                        if "env" in sc else "") + f" (~{est:.0f} min)")
            if sc.get("batches") and not self.ensure_prewarmed(env_key):
                self.mark(name, "failed", reason=f"prewarm of {env_key} failed")
                if scope == "env":
                    env_stopped.append(env_key)
                    self.save()
                    continue
                return self.finish("NO-GO", f"prewarm of {env_key} failed on {self.state.get('prewarm_failed')}")
            self.mark(name, "running", env=env_key, tag=self.stage_tag(sc))
            specs = self.stage_gates(sc)
            gate_attempts = int(sc.get("gate_attempts", 1))
            results: dict[str, dict] = {}
            for g_attempt in range(1, gate_attempts + 1):
                ents = self.stage_batches(sc)
                for ent in ents:
                    # Legacy stages (no env:) keep deploy.sh's DEFAULT_ENV from testbed.conf.
                    self.run_batch(ent["yaml"], ent["extra"],
                                   env_name=self.envs.get(ent["env"]) if ent["explicit_env"] else None,
                                   tag=ent["tag"], sha=self.sha_for(ent["env"]))
                results = {}
                for spec in specs:
                    gid = self.gate_id(spec["gate"], name)
                    results[gid] = self._gate_for_stage(spec, ents, env_key if "env" in sc else None, gid)
                failed = [g for g, r in results.items() if r.get("status") in ("FAIL", "INCOMPLETE")
                          and self._counts(sc, r.get("gate", g))]
                if not failed or g_attempt == gate_attempts or self.args.dry_run:
                    break
                # One transient hiccup must not end an unattended campaign: set the
                # failed runs aside (kept, renamed) and run the stage once more.
                self.log(f"{failed} FAIL/INCOMPLETE — re-running stage {name} once "
                         f"(attempt {g_attempt + 1}/{gate_attempts})")
                self.event("gate_retry", gate=",".join(failed), stage=name)
                for gid in results:
                    if (self.dir / "gates" / f"{gid}.json").is_file():
                        shutil.copy(self.dir / "gates" / f"{gid}.json",
                                    self.dir / "gates" / f"{gid}.attempt{g_attempt}.json")
                tag_fail = (specs[0]["gate"] if specs else "gate").upper()
                for ent in ents:
                    # Only this batch's cells: a tagged dir may also hold other stages' runs.
                    cells = {c.id for c in load_batch(gates.tpath(ent["yaml"])).cells}
                    for r in gates.run_dirs(ent["dir"], include_warmup=True):
                        if (gates.cell_of(r) or ("",))[0] in cells:
                            r.rename(r.with_name(r.name + f".{tag_fail}FAIL-{stamp()}"))
                    self.state["batches"].pop(self.batch_key(ent), None)
                self.save()
            status = gates.worst(r.get("status") for r in results.values()) if results else None
            # A dry-run gate without fixture inputs was never evaluated: it cannot stop anything.
            stop_status = gates.worst(r.get("status") for g, r in results.items()
                                      if self._counts(sc, r.get("gate", g))
                                      and not r.get("not_evaluated")) if results else None
            if sc.get("stop_on_fail") and stop_status in ("FAIL", "INCOMPLETE"):
                status = stop_status
                self.mark(name, "done", gate=status, gates={g: r.get("status") for g, r in results.items()},
                          env=env_key, tag=self.stage_tag(sc))
                if scope == "env":
                    env_stopped.append(env_key)
                    self.save()
                    self.log(f"{status} with stop_on_fail — skipping the remaining {env_key} stages")
                    continue
                self.log(f"{status} with stop_on_fail — skipping remaining stages")
                stop = True
                break
            self.mark(name, "done", gate=status, gates={g: r.get("status") for g, r in results.items()},
                      env=env_key, tag=self.stage_tag(sc))
        return self.finish(None, None, stopped=stop)

    @staticmethod
    def _counts(sc: dict, gate: str) -> bool:
        """Does this gate's FAIL trigger the stage's retry / stop_on_fail? A list
        `stop_on_fail: [g0]` names the gates that do; otherwise every gate does."""
        sof = sc.get("stop_on_fail")
        return gate in sof if isinstance(sof, list) else True

    def _gate_for_stage(self, spec: dict, ents: list[dict], env_key: str | None, gid: str) -> dict:
        """Evaluate one gate on the stage's batch dirs. `env_key` (multi-env configs
        only; None for legacy stages) selects the expected sha and build env."""
        gate = spec["gate"]
        dirs = {e["name"]: e["dir"] for e in ents}
        extra: list[str] = []
        if gate == "g0":
            paths = self._fixture("g0") if self.args.dry_run else [str(p) for p in dirs.values()]
        elif gate == "g1":
            if self.args.dry_run:
                paths = [s for s in (self.args.fixture_runs or [])
                         if s.split("=")[0] in ("formation", "13node", "reach16", "dataslots")]
            else:
                kinds = {**G1_KINDS, **(self.cfg.get("g1_kinds") or {})}
                paths = []
                for b, d in dirs.items():
                    base = b.rpartition("__")[0] if "__" in b else b
                    if base in kinds:
                        paths.append(f"{kinds[base]}={d}")
        elif gate == "ab":
            # This stage's batch dirs plus those of the stages it is paired with.
            all_dirs = list(dirs.values())
            for other in spec.get("with_stages", []):
                osc = next((x for x in self.cfg.get("stages", []) if x["name"] == other), None)
                if osc:
                    all_dirs += [e["dir"] for e in self.stage_batches(osc)]
            paths = self._fixture("ab") if self.args.dry_run else [str(p) for p in dict.fromkeys(all_dirs)]
        elif gate == "sleep":
            paths = self._fixture("sleep") if self.args.dry_run else [str(p) for p in dirs.values()]
        else:
            paths = self._fixture("g2") if self.args.dry_run else [str(p) for p in dirs.values()]
            req = spec.get("required")
            if req:
                extra += ["--required", ",".join(req)]
        if not paths:
            self.log(f"{gid}: no inputs (dry-run without --fixture-runs?) — gate not evaluated")
            res = {"gate": gate, "status": "INCOMPLETE", "checks": [], "not_evaluated": True}
            self.state["gates"][gid] = {"status": "INCOMPLETE", "gate": gate, "env": env_key, "ts": now(),
                                        "not_evaluated": True}
            self.save()
            return res
        return self.run_gate(gate, paths + extra, gate_id=gid, env_key=env_key)

    def run_ab(self, ab: dict) -> bool:
        env_old = gates.dig(self.cfg, "envs.old", "ttgo-t-beam-v2-old")
        env_new = gates.dig(self.cfg, "envs.new", "ttgo-t-beam-v2")
        if not self.args.dry_run and not self.prewarm(env_old):
            return False
        env = {"V1_ENV": env_old, "V2_ENV": env_new, "V1_TAG": ab.get("v1_tag", "lm134"),
               "V2_TAG": ab.get("v2_tag", "lm200"), "V1_LABEL": ab.get("v1_label", ""),
               "V2_LABEL": ab.get("v2_label", ""), "ROUNDS": str(ab.get("rounds", 3)),
               "BATCHES": ab["batch"], "SKIP_UPGRADE": "1",
               "FIG_ROOT": str(self.dir / "figures_ab"),
               "EXPECTED_SHA": self.state.get("lorachat_head", "")}
        if self.args.dry_run:
            self.log(f"(dry-run) would run run_full_redo.sh with {env}")
            return True
        rc = self.sh(["bash", str(TESTBED / "run_full_redo.sh")], log_name="ab.log", env=env)
        self.state["ab"] = {"rc": rc, "v1": f"runs/{ab['batch']}__{env['V1_TAG']}",
                            "v2": f"runs/{ab['batch']}__{env['V2_TAG']}"}
        self.save()
        return rc == 0

    def finish(self, forced_status: str | None, reason: str | None, stopped: bool = False) -> int:
        self.state["status"] = "stopped" if (stopped or forced_status) else "finished"
        self.save()
        if self.selected("report") or forced_status:
            cmd = [sys.executable, str(HERE / "report.py"), "--campaign-dir", str(self.dir)]
            if self.args.config:
                cmd += ["--config", str(self.args.config)]
            if forced_status:
                cmd += ["--force-status", forced_status, "--reason", reason or ""]
            self.sh(cmd, log_name="report.log")
            self.mark("report", "done")
        if self.args.dry_run:
            # Throw-away render dirs: only ever contain config.yaml from run_batch --dry-run.
            for d in sorted((TESTBED / "runs").glob("lm200dry_*")):
                if all(f.name == "config.yaml" for f in d.rglob("*") if f.is_file()):
                    shutil.rmtree(d)
                    self.log(f"dry-run: removed {d.relative_to(TESTBED)}")
        verdict = {}
        vp = self.dir / "report" / "verdict.json"
        if vp.is_file():
            verdict = json.loads(vp.read_text())
        status = verdict.get("verdict", forced_status or "UNKNOWN")
        self.state["status"] = "stopped" if (stopped or forced_status) else "finished"
        self.state["verdict"] = status
        self.save()
        summary = f"lm200 campaign {self.state['status']}: {status}. {verdict.get('headline', reason or '')}"
        self.log(summary)
        self.log(f"report: {self.dir / 'report' / 'report.md'}")
        if status == "GO":
            self.log("GO: to adopt, merge lm200-eval into new_loramesher yourself and set "
                     "GIT_BRANCH back in scripts/testbed/testbed.conf (the campaign does neither).")
        self.notify(status, summary)
        return 0 if status in ("GO", "GO-WITH-CAVEATS") else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", type=Path, default=None, help="default: lm200/campaign.yaml")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--fixture-runs", nargs="*", default=None, metavar="KIND=DIR",
                    help="dry-run gate inputs: g0=DIR g2=DIR ab=DIR sleep=DIR formation=NEW[:BASE] 13node=... ")
    ap.add_argument("--from-stage", help="preflight, prewarm, a stage name from the config, or report")
    ap.add_argument("--only-stage", nargs="+")
    ap.add_argument("--expect-sha", default=None,
                    help="override lib_sha for every env (e.g. 134ae25 for a fixture self-test)")
    ap.add_argument("--expect-radiolib", default=None,
                    help="override expected_radiolib ('' disables; for July fixture self-tests)")
    ap.add_argument("--reset-state", action="store_true",
                    help="archive state.json and start over (run dirs are kept)")
    a = ap.parse_args(argv)
    return Campaign(a).run()


if __name__ == "__main__":
    raise SystemExit(main())
