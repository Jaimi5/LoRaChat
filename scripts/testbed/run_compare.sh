#!/usr/bin/env bash
# run_compare.sh — run one or more batches under BOTH LoRaMesher versions (v1 and
# v2) in a single unattended invocation, keeping each version's data in its own
# run dir, then aggregate + plot the v1-vs-v2 comparison.
#
# It automates the manual "run the same batch twice, once per PlatformIO env,
# into two runs/ trees" workflow documented in batches/sim_load_compare.yaml, so
# you can start it once and leave it for days.
#
# Usage
# -----
#   scripts/testbed/run_compare.sh [BATCH ...]
#
#   BATCH is a batch name or a path, e.g. `sim_size_compare`,
#   `sim_size_compare.yaml`, or `batches/sim_size_compare.yaml`. With no
#   arguments it runs the two v2-demonstration batches:
#       sim_size_compare  sim_load_compare_full
#
#   For each batch it runs the full v1 pass, then the full v2 pass, moving the
#   output to runs/<batch>_v1 and runs/<batch>_v2, then writes aggregated.json
#   for each and the comparison figures under docs/paper/figures/<batch>/. Doing
#   both versions of a batch before moving to the next keeps them as close in
#   time as a two-pass design allows.
#
#   WEATHER CAVEAT (sim_load_compare_full only): the two passes are still whole
#   sequential passes, so a given SF cell's v1 and v2 runs are ~one pass (hours)
#   apart — the 3.5 km long link can drift between them. This script does NOT do
#   cell-level interleaving (that needs a reflash per cell). Mitigate by reading
#   the 006C<->3428 `linkstats` in both passes and discarding any cell where the
#   link state differed; see batches/sim_load_compare_full.yaml. The size sweep
#   runs on the isolated main cluster and has no such confound.
#
# Run it unattended (survives logout, logs everything):
#   cd /path/to/LoRaChat
#   nohup scripts/testbed/run_compare.sh sim_size_compare sim_load_compare_full \
#         > compare_$(date +%Y%m%d).log 2>&1 &
#   tail -f compare_*.log        # watch progress
#
# Knobs (environment variables)
# -----------------------------
#   V1_ENV          PlatformIO env for v1   (default: ttgo-t-beam)
#   V2_ENV          PlatformIO env for v2   (default: ttgo-t-beam-v2)
#   FIG_ROOT        figure output root      (default: docs/paper/figures)
#   EXTRA_RUN_ARGS  extra flags forwarded to run_batch.py (e.g.
#                   "--skip-clock-check --reject-skew-ms 60000")
#   SKIP_ANALYSIS   set to 1 to skip the multirun/plot step (data only)
#
# Resuming: if a version's output dir (runs/<batch>_v1 or _v2) already exists and
# is non-empty, it is moved aside to <dir>.bak-<UTCSTAMP> rather than clobbered,
# and that pass re-runs fresh. To resume a crashed campaign instead, re-run just
# the missing env pass by hand with `DEFAULT_ENV=<env> runner/run_batch.py
# batches/<batch>.yaml --skip-upgrade` and then run this script's analysis step.
set -uo pipefail

V1_ENV="${V1_ENV:-ttgo-t-beam}"
V2_ENV="${V2_ENV:-ttgo-t-beam-v2}"
FIG_ROOT="${FIG_ROOT:-docs/paper/figures}"
EXTRA_RUN_ARGS="${EXTRA_RUN_ARGS:-}"
SKIP_ANALYSIS="${SKIP_ANALYSIS:-0}"

# Run from the testbed root so `batches/<x>.yaml` resolves the way run_batch.py
# expects, regardless of where the user invoked us from.
TESTBED_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$TESTBED_ROOT"

UPGRADE_DONE=0          # gateways are git-pulled/pkg-updated once, on the first pass
OVERALL_RC=0

ts()   { date -u +%Y%m%dT%H%M%SZ; }
log()  { printf '%s  %s\n' "$(ts)" "$*"; }
banner() { echo; echo "============================================================"; log "$*"; echo "============================================================"; }

# Resolve a batch argument (name / file / path) to a batches/*.yaml path.
resolve_yaml() {
  local a="$1" base
  base="$(basename "$a")"; base="${base%.yaml}"
  local cand="batches/${base}.yaml"
  if [[ -f "$cand" ]]; then echo "$cand"; return 0; fi
  if [[ -f "$a" ]]; then echo "$a"; return 0; fi
  return 1
}

# The batch.name inside the YAML determines runs/<name>; read it authoritatively.
batch_name_of() {
  python3 - "$1" <<'PY'
import sys; sys.path.insert(0, ".")
from pathlib import Path
from runner.batch import load_batch
print(load_batch(Path(sys.argv[1])).name)
PY
}

# Rough wall-clock estimate for ONE pass (sum of warmup+duration+cooldown over
# all cells×reps; excludes per-cell upload/reset overhead of a few minutes each).
est_minutes_of() {
  python3 - "$1" <<'PY'
import sys; sys.path.insert(0, ".")
from pathlib import Path
from runner.batch import load_batch
b = load_batch(Path(sys.argv[1]))
tot = 0.0
start = 0 if b.warmup_run else 1
for c in b.cells:
    for r in range(start, b.runs + 1):
        if r == 0:
            tot += b.boot_verify_sec / 60.0
            continue
        wu = c.warmup_min   if c.warmup_min   is not None else b.warmup_min
        du = c.duration_min if c.duration_min is not None else b.duration_min
        cd = c.cooldown_min if c.cooldown_min is not None else b.cooldown_min
        tot += wu + du + cd
print(int(round(tot)))
PY
}

# Run one version pass and move its output to runs/<name>_<tag>.
run_pass() {
  local yaml="$1" name="$2" env="$3" tag="$4"
  local src="runs/${name}" dst="runs/${name}_${tag}"

  # Never clobber: stash a leftover intermediate dir and any prior version dir.
  [[ -d "$src" ]] && mv "$src" "${src}.stale-$(ts)" && log "moved stale $src aside"
  if [[ -d "$dst" ]] && [[ -n "$(ls -A "$dst" 2>/dev/null)" ]]; then
    mv "$dst" "${dst}.bak-$(ts)"; log "existing $dst backed up (bak-*)"
  fi

  local extra="$EXTRA_RUN_ARGS"
  [[ "$UPGRADE_DONE" == "1" ]] && extra="$extra --skip-upgrade"

  banner "$name [$tag] — flashing $env, offered load starts after warmup"
  log "DEFAULT_ENV=$env runner/run_batch.py batches/$(basename "$yaml") $extra"
  DEFAULT_ENV="$env" python3 runner/run_batch.py "$yaml" $extra
  local rc=$?
  UPGRADE_DONE=1
  [[ $rc -ne 0 ]] && { OVERALL_RC=1; log "WARNING: run_batch for $name [$tag] returned rc=$rc (some runs may have been rejected); continuing"; }

  if [[ -d "$src" ]]; then
    mv "$src" "$dst"; log "collected $tag data -> $dst"
  else
    log "ERROR: expected output $src not found after $name [$tag]"; OVERALL_RC=1
  fi
}

analyse() {
  local name="$1"
  local v1="runs/${name}_v1" v2="runs/${name}_v2"
  [[ "$SKIP_ANALYSIS" == "1" ]] && { log "SKIP_ANALYSIS=1 — leaving $v1 / $v2 unaggregated"; return; }
  [[ -d "$v1" && -d "$v2" ]] || { log "skip analysis for $name — missing $v1 or $v2"; return; }

  banner "$name — aggregate + plot v1 vs v2"
  python3 analysis/multirun.py "$v1" || { OVERALL_RC=1; log "multirun failed on $v1"; }
  python3 analysis/multirun.py "$v2" || { OVERALL_RC=1; log "multirun failed on $v2"; }

  # x-axis + topology heuristics: size sweep -> payload x-axis; full topology ->
  # 16-node / deeper-hop model projection. Override by editing here if needed.
  local xarg="" ; case "$name" in *size*) xarg="--x size" ;; esac
  local topo=(--nodes 13 --max-hops 3) ; case "$name" in *full*) topo=(--nodes 16 --max-hops 5) ;; esac

  local out="${FIG_ROOT}/${name}"
  python3 analysis/plot_compare.py --v1 "$v1" --v2 "$v2" --out "$out" "${topo[@]}" $xarg \
    && log "figures written to $out" \
    || { OVERALL_RC=1; log "plot_compare failed for $name"; }
}

main() {
  local args=("$@")
  [[ ${#args[@]} -eq 0 ]] && args=(sim_size_compare sim_load_compare_full)

  # Resolve + validate everything up front so a typo fails fast, not 20 h in.
  local yamls=() names=() y n grand=0
  for a in "${args[@]}"; do
    y="$(resolve_yaml "$a")" || { echo "error: no batch YAML for '$a' (looked in batches/)"; exit 2; }
    n="$(batch_name_of "$y")" || { echo "error: could not load batch '$y'"; exit 2; }
    yamls+=("$y"); names+=("$n")
  done

  banner "v1-vs-v2 compare campaign — $(date -u)"
  log "v1 env: $V1_ENV     v2 env: $V2_ENV"
  for i in "${!yamls[@]}"; do
    local m; m="$(est_minutes_of "${yamls[$i]}")"
    local both=$(( m * 2 ))
    grand=$(( grand + both ))
    log "batch ${names[$i]}: ~${m} min/pass x2 versions = ~${both} min (~$(( both / 60 )) h)"
  done
  log "estimated total (excl. per-cell flash overhead): ~${grand} min (~$(( grand / 60 )) h)"

  for i in "${!yamls[@]}"; do
    y="${yamls[$i]}"; n="${names[$i]}"
    run_pass "$y" "$n" "$V1_ENV" v1
    run_pass "$y" "$n" "$V2_ENV" v2
    analyse "$n"
  done

  banner "campaign done — overall rc=$OVERALL_RC"
  [[ $OVERALL_RC -eq 0 ]] && log "all passes completed; figures under ${FIG_ROOT}/<batch>/" \
                          || log "completed with warnings/errors — grep this log for WARNING/ERROR"
  return $OVERALL_RC
}

main "$@"
