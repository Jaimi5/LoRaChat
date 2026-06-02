#!/usr/bin/env python3
"""CLI entry: execute a batch of paper experiments end-to-end.

Usage:
    run_batch.py batches/formation.yaml [--runs N] [--dry-run]
                                        [--skip-clock-check]
                                        [--reject-skew-ms 30000]
                                        [--warn-skew-ms 1000]
                                        [--skip-upload]
                                        [--skip-upgrade]

Before any batch runs, the runner invokes `deploy.sh upgrade` once to git-pull
and `pio pkg update` every gateway, so the firmware compiled and flashed by
the per-cell upload step reflects the latest checked-in code. Pass
`--skip-upgrade` to bypass (e.g. resuming an interrupted batch).

For each cell × repetition, this materializes a per-run experiment YAML,
calls deploy.sh to push it / upload firmware (first run of each cell only,
unless --skip-upload) / start monitors, waits warmup→duration→cooldown,
stops monitors, pulls logs, and stores everything under
    scripts/testbed/runs/<batch>/<YYYYMMDD-HHMMSS>-<cell>-r<NN>/

Between repetitions of the same cell only a hardware reset is issued
(via deploy.sh reset), not a full reflash.

Clock skew is measured per gateway before warmup (t0) and again after
stop-monitor (t1). Each gateway's monitor log timestamps are then
rewritten in place using the linearly-interpolated offset; originals are
preserved as `*.raw[.gz]`. The run is aborted only if the worst pre-run
skew exceeds --reject-skew-ms (default 30s), since anything smaller can
be corrected post hoc. Offsets between --warn-skew-ms and the reject
ceiling produce a warning but the run proceeds.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

# Make the runner package importable when invoked as a script.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from runner.batch import Batch, Cell, load_batch, render_run_yaml
from runner.clock_check import (
    SkewReport,
    list_device_map,
    measure_offsets,
)
from runner.lifecycle import (
    RunRecorder,
    collect_logs,
    deploy_config,
    mark_measurement_window,
    start_monitors,
    stop_monitors,
    wait_phase,
)
from runner.log_collector import move_session_logs


def _utc_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")


def _samples_to_json(report: SkewReport | None) -> dict | None:
    if report is None:
        return None
    return {
        "measured_at_ms": report.measured_at_ms,
        "max_abs_offset_ms": report.max_abs_offset_ms,
        "samples": [
            {
                "gw_id": s.gw_id,
                "offset_ms": s.offset_ms,
                "host_epoch_ms": s.host_epoch_ms,
                "reachable": s.reachable,
            }
            for s in report.samples
        ],
    }


def _resolve_batch_paths(inputs: list[Path]) -> list[Path] | None:
    """Expand each input — a file or a directory of `*.yaml` — into a flat
    list of batch YAMLs. Returns None if any path is invalid or any directory
    contains no YAMLs."""
    out: list[Path] = []
    for p in inputs:
        p = p.resolve()
        if p.is_dir():
            yamls = sorted(p.glob("*.yaml"))
            if not yamls:
                print(f"error: no *.yaml in directory {p}", file=sys.stderr)
                return None
            out.extend(yamls)
        elif p.is_file():
            out.append(p)
        else:
            print(f"error: batch path not found: {p}", file=sys.stderr)
            return None
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("batch_yaml", type=Path, nargs="+",
                    help="One or more batch YAML files, or a directory of them "
                         "(e.g. `batches/` to run every batch sequentially).")
    ap.add_argument("--runs", type=int, default=None, help="Override batch.runs")
    ap.add_argument("--dry-run", action="store_true",
                    help="Render per-run YAMLs but do not invoke deploy.sh")
    ap.add_argument("--skip-clock-check", action="store_true",
                    help="Do not measure clock offsets and do not correct logs")
    ap.add_argument("--reject-skew-ms", "--max-skew-ms", type=int, default=30000,
                    dest="reject_skew_ms",
                    help="Reject a run if pre-run worst skew exceeds this "
                         "(default 30000 = 30s — corrected silently below this)")
    ap.add_argument("--warn-skew-ms", type=int, default=1000,
                    help="Print a warning if pre-run worst skew exceeds this "
                         "(default 1000)")
    ap.add_argument("--skip-upload", action="store_true",
                    help="Do not flash firmware between cells (assume already flashed)")
    ap.add_argument("--skip-upgrade", action="store_true",
                    help="Do not run `deploy.sh upgrade` (git pull + pio pkg update) "
                         "before the batch. By default the runner upgrades gateways "
                         "once per invocation so flashed firmware reflects the "
                         "latest checked-in code.")
    args = ap.parse_args(argv)

    batch_paths = _resolve_batch_paths(args.batch_yaml)
    if batch_paths is None:
        return 2

    # Resolve paths relative to scripts/testbed/.
    testbed_root = Path(__file__).resolve().parents[1]
    repo_root = testbed_root.parents[1]
    deploy_sh = testbed_root / "deploy.sh"
    if not deploy_sh.is_file():
        print(f"error: deploy.sh not found at {deploy_sh}", file=sys.stderr)
        return 2

    # Upgrade every gateway once per invocation, before any batch runs. Keeps
    # `cmd_upload` (which compiles from the gateway's local checkout via
    # `pio run --target upload`) honest — otherwise stale code on a gateway
    # would silently end up on the devices. Strict: any non-zero from
    # cmd_upgrade aborts, since a partially-upgraded fleet would produce
    # mixed-version data within the same batch.
    if not args.skip_upgrade and not args.dry_run:
        print("=== Upgrading gateways (git pull + pio pkg update) ===", flush=True)
        rc = subprocess.call(["bash", str(deploy_sh), "upgrade"], cwd=repo_root)
        if rc != 0:
            print(f"error: deploy.sh upgrade failed (rc={rc}); aborting batch run. "
                  f"Re-run with --skip-upgrade to bypass after fixing the gateway(s).",
                  file=sys.stderr)
            return rc

    # Build SHORT_ID → GW_ID map once for the whole run (same testbed across
    # batches). Used to label the per-gateway skew record in clock_offsets.json.
    short_id_to_gw: dict[str, str] = {}
    if not args.skip_clock_check:
        try:
            short_id_to_gw = list_device_map(deploy_sh, repo_root)
        except RuntimeError as e:
            print(f"warning: could not load device map ({e}); skew record disabled")

    local_log_dir = (repo_root / "logs_testbed").resolve()

    if len(batch_paths) > 1:
        print(f"=== running {len(batch_paths)} batch(es) in order: "
              f"{', '.join(p.stem for p in batch_paths)} ===\n")

    per_batch: list[tuple[str, int, int]] = []   # (name, total, failures)
    for batch_path in batch_paths:
        batch = load_batch(batch_path)
        if args.runs is not None:
            batch = batch.__class__(**{**batch.__dict__, "runs": args.runs})

        total, failures = _run_one_batch(
            batch, args, testbed_root, repo_root, deploy_sh,
            short_id_to_gw, local_log_dir,
        )
        per_batch.append((batch.name, total, failures))

    grand_total = sum(t for _, t, _ in per_batch)
    grand_failures = sum(f for _, _, f in per_batch)

    if len(per_batch) > 1:
        print("\n=== overall ===")
        for name, t, f in per_batch:
            print(f"  {name:20s} {t - f}/{t} runs OK")
        print(f"  {'TOTAL':20s} {grand_total - grand_failures}/{grand_total} runs OK")
    return 0 if grand_failures == 0 else 1


def _run_one_batch(
    batch: Batch,
    args: argparse.Namespace,
    testbed_root: Path,
    repo_root: Path,
    deploy_sh: Path,
    short_id_to_gw: dict[str, str],
    local_log_dir: Path,
) -> tuple[int, int]:
    """Execute every (cell × repetition) run of one batch. Returns
    (total_runs, failures). Common setup (paths, device map) is the caller's
    responsibility so it can be shared across batches."""
    batch_root = testbed_root / "runs" / batch.name
    batch_root.mkdir(parents=True, exist_ok=True)

    print(f"=== batch: {batch.name} ({batch.description}) ===")
    print(f"  base:     {batch.base_yaml}")
    print(f"  cells:    {len(batch.cells)}")
    print(f"  runs/cell:{batch.runs}")
    print(f"  duration: {batch.duration_min} min   warmup: {batch.warmup_min} min   "
          f"cooldown: {batch.cooldown_min} min")
    print(f"  output:   {batch_root}")

    total_runs = len(batch.cells) * batch.runs
    print(f"  total:    {total_runs} runs")

    failures = 0
    start_run = 0 if batch.warmup_run else 1
    for cell_index, cell in enumerate(batch.cells, 1):
        for run_index in range(start_run, batch.runs + 1):
            is_warmup = (run_index == 0)
            run_id = f"{_utc_stamp()}-{cell.id}-r{run_index:02d}"
            session = f"{batch.name}-{run_id}"
            run_dir = batch_root / session
            run_dir.mkdir(parents=True, exist_ok=True)

            rep_label = "warmup" if is_warmup else f"{run_index}/{batch.runs}"
            print(f"\n--- run {cell_index}/{len(batch.cells)} cell={cell.id} "
                  f"rep={rep_label} session={session} ---")

            run_yaml = render_run_yaml(batch, cell, run_index, run_dir)
            print(f"  rendered: {run_yaml}")

            if args.dry_run:
                continue

            recorder = RunRecorder(run_dir)

            report_t0: SkewReport | None = None
            if not is_warmup and not args.skip_clock_check:
                p = recorder.begin("clock-check-t0")
                report_t0 = measure_offsets(deploy_sh, repo_root)
                worst = report_t0.max_abs_offset_ms
                recorder.end(
                    p,
                    ok=report_t0.ok(args.reject_skew_ms),
                    max_abs_offset_ms=worst,
                )
                if worst > args.reject_skew_ms:
                    print(f"  SKEW EXCEEDED: {worst}ms > {args.reject_skew_ms}ms "
                          f"— rejecting run")
                    failures += 1
                    continue
                if worst > args.warn_skew_ms:
                    print(f"  warning: worst pre-run skew is {worst}ms "
                          f"(> {args.warn_skew_ms}ms) — recorded but not corrected")

            if is_warmup:
                do_upload, do_reset_only = True, False
            elif batch.warmup_run and run_index == 1:
                do_upload, do_reset_only = False, True
            else:
                first_of_cell = (run_index == 1)
                do_upload = first_of_cell and not args.skip_upload
                do_reset_only = batch.reset_between_runs and not do_upload

            if not deploy_config(
                deploy_sh, repo_root, run_yaml, session,
                do_upload=do_upload,
                do_reset_only=do_reset_only,
                recorder=recorder,
            ):
                failures += 1
                continue

            if not start_monitors(deploy_sh, repo_root, session, recorder):
                failures += 1
                continue

            if is_warmup:
                wait_phase("boot-verify", batch.boot_verify_sec, recorder)
                stop_monitors(deploy_sh, repo_root, recorder)
                continue

            warmup = (cell.warmup_min if cell.warmup_min is not None else batch.warmup_min) * 60
            duration = (cell.duration_min if cell.duration_min is not None else batch.duration_min) * 60
            cooldown = (cell.cooldown_min if cell.cooldown_min is not None else batch.cooldown_min) * 60

            wait_phase("warmup", warmup, recorder)
            mark_measurement_window(recorder, "measurement-start",
                                    cell=cell.id, run=run_index)
            wait_phase("measurement", duration, recorder)
            mark_measurement_window(recorder, "measurement-end",
                                    cell=cell.id, run=run_index)
            wait_phase("cooldown", cooldown, recorder)

            stop_monitors(deploy_sh, repo_root, recorder)

            # Second clock sample, taken as close as possible to the last log
            # line each gateway wrote. Recorded for diagnostics only.
            report_t1: SkewReport | None = None
            if not args.skip_clock_check:
                p = recorder.begin("clock-check-t1")
                try:
                    report_t1 = measure_offsets(deploy_sh, repo_root)
                    recorder.end(
                        p,
                        ok=True,
                        max_abs_offset_ms=report_t1.max_abs_offset_ms,
                    )
                except Exception as e:
                    recorder.end(p, ok=False, error=str(e))
                    print(f"  warning: t1 clock-check failed ({e})")

            collect_logs(deploy_sh, repo_root, session, recorder)

            logs_dir = run_dir / "logs"
            moved = move_session_logs(local_log_dir, session, logs_dir)
            print(f"  moved {moved} log file(s) into {logs_dir}")

            if report_t0 is not None and short_id_to_gw:
                # Skew is recorded for diagnostics only; it is no longer applied
                # to the logs — the raw monitor capture is kept as-is.
                offsets_path = run_dir / "clock_offsets.json"
                offsets_path.write_text(json.dumps({
                    "reject_skew_ms": args.reject_skew_ms,
                    "warn_skew_ms": args.warn_skew_ms,
                    "device_map": short_id_to_gw,
                    "t0": _samples_to_json(report_t0),
                    "t1": _samples_to_json(report_t1),
                }, indent=2))

    print(f"\n=== batch complete: {total_runs - failures}/{total_runs} runs OK ===")
    return total_runs, failures


if __name__ == "__main__":
    raise SystemExit(main())
