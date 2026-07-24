"""Phase machine that drives one run of an experiment.

A run consists of: (1) deploy_config -> push-config + upload the per-run YAML,
or just reset between runs of the same cell; (2) start monitors; (3) warmup
wait; (4) measurement marker -> wait `duration_min`; (5) end marker; (6)
stop monitors; (7) cooldown wait; (8) pull logs.

This module shells out to `deploy.sh` rather than reimplementing SSH/USB
orchestration. All subprocesses inherit stdout/stderr so the user sees
deploy.sh's existing colored output live.
"""

from __future__ import annotations

import json
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass
class Phase:
    name: str
    start_iso: str
    end_iso: str | None = None
    ok: bool | None = None
    extra: dict | None = None


class RunRecorder:
    """Append-only JSON log of phase transitions for a single run."""

    def __init__(self, run_dir: Path):
        self.path = run_dir / "lifecycle.json"
        self.phases: list[Phase] = []
        self._flush()

    def begin(self, name: str, **extra) -> Phase:
        p = Phase(name=name, start_iso=_now_iso(), extra=extra or None)
        self.phases.append(p)
        self._flush()
        return p

    def end(self, phase: Phase, ok: bool, **extra) -> None:
        phase.end_iso = _now_iso()
        phase.ok = ok
        if extra:
            phase.extra = {**(phase.extra or {}), **extra}
        self._flush()

    def _flush(self) -> None:
        data = [
            {
                "name": p.name,
                "start": p.start_iso,
                "end": p.end_iso,
                "ok": p.ok,
                **(p.extra or {}),
            }
            for p in self.phases
        ]
        self.path.write_text(json.dumps(data, indent=2))


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _run(cmd: list[str], cwd: Path) -> int:
    """Stream-execute a deploy.sh invocation; return the exit code."""
    print(f"$ {' '.join(cmd)}", flush=True)
    return subprocess.call(cmd, cwd=cwd)


def deploy_config(
    deploy_sh: Path,
    repo_root: Path,
    run_yaml: Path,
    session: str,
    do_upload: bool,
    do_reset_only: bool,
    recorder: RunRecorder,
) -> bool:
    """Push the per-run config and (optionally) upload firmware or just reset.

    do_upload=True: full push-config + upload (used at the start of each cell).
    do_reset_only=True: push-config only + hardware reset (between repetitions
        within a cell — config may or may not actually differ, but pushing is
        cheap and harmless).
    """
    if do_upload:
        # Stop any leftover monitors before pio grabs the serial ports.
        # `deploy.sh upload` (unlike `deploy.sh all`) does NOT stop monitors
        # itself, so a stale monitor from a previous session would hold the
        # port and make `pio run -t upload` fail. Idempotent — returns 0
        # even when no monitors are running.
        p = recorder.begin("pre-upload-stop-monitor")
        rc = _run(["bash", str(deploy_sh), "stop-monitor"], cwd=repo_root)
        recorder.end(p, ok=(rc == 0), rc=rc)

        p = recorder.begin("upload", yaml=str(run_yaml))
        rc = _run(
            ["bash", str(deploy_sh), "upload", "-n", session, "-x", str(run_yaml)],
            cwd=repo_root,
        )
        recorder.end(p, ok=(rc == 0), rc=rc)
        if rc != 0:
            return False

        # Synchronized reset AFTER the staggered upload. Boards are flashed one at a
        # time across the ~15-min upload, and each SIM_TESTBED_WARMUP_MS clock starts
        # at that board's flash-time boot. Without this reset, boards flashed early
        # finish their warmup and emit the first burst packets BEFORE the monitors
        # attach (which happens only after the whole upload completes) — silently
        # dropping seq 0/1 on the earliest boards, a systematic PDR bias against the
        # slower-to-flash v2. One reset reboots every board at the same instant so all
        # warmup clocks restart together; the caller then attaches monitors (ports are
        # free — the reset must precede start_monitors, since gw-reset needs the port
        # to pulse DTR/RTS). Mirrors the do_reset_only path, which never had this bug.
        p = recorder.begin("post-upload-sync-reset")
        rc = _run(["bash", str(deploy_sh), "reset", "-n", session], cwd=repo_root)
        recorder.end(p, ok=(rc == 0), rc=rc)
        return rc == 0
    if do_reset_only:
        p = recorder.begin("push-config", yaml=str(run_yaml))
        rc = _run(
            ["bash", str(deploy_sh), "push-config", "-x", str(run_yaml)],
            cwd=repo_root,
        )
        recorder.end(p, ok=(rc == 0), rc=rc)
        if rc != 0:
            return False

        p = recorder.begin("reset")
        rc = _run(["bash", str(deploy_sh), "reset", "-n", session], cwd=repo_root)
        recorder.end(p, ok=(rc == 0), rc=rc)
        return rc == 0
    return True


def start_monitors(deploy_sh: Path, repo_root: Path, session: str, recorder: RunRecorder) -> bool:
    p = recorder.begin("monitor-start", session=session)
    rc = _run(["bash", str(deploy_sh), "monitor", "-n", session], cwd=repo_root)
    recorder.end(p, ok=(rc == 0), rc=rc)
    return rc == 0


def stop_monitors(deploy_sh: Path, repo_root: Path, recorder: RunRecorder) -> bool:
    p = recorder.begin("monitor-stop")
    rc = _run(["bash", str(deploy_sh), "stop-monitor"], cwd=repo_root)
    recorder.end(p, ok=(rc == 0), rc=rc)
    return rc == 0


def collect_logs(deploy_sh: Path, repo_root: Path, session: str, recorder: RunRecorder) -> bool:
    p = recorder.begin("collect-logs", session=session)
    rc = _run(["bash", str(deploy_sh), "logs", "-n", session], cwd=repo_root)
    recorder.end(p, ok=(rc == 0), rc=rc)
    return rc == 0


def wait_phase(name: str, seconds: float, recorder: RunRecorder) -> None:
    """Sleep for `seconds`, recording start/end."""
    p = recorder.begin(name, seconds=seconds)
    print(f"[lifecycle] {name}: sleeping {seconds:.0f}s", flush=True)
    time.sleep(seconds)
    recorder.end(p, ok=True)


def mark_measurement_window(recorder: RunRecorder, label: str, **extra) -> None:
    """Record an instantaneous marker — used to bracket the measurement window
    within the warmup/cooldown sleeps. Analysis filters log events to this
    window when computing steady-state metrics."""
    p = recorder.begin(label, **extra)
    recorder.end(p, ok=True)
