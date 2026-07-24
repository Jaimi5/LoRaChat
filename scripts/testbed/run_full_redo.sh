#!/usr/bin/env bash
# run_full_redo.sh — one-shot, resumable, interleaved v1-vs-v2 recollection.
#
# Runs BOTH comparison batches x N rounds x BOTH library versions in a SINGLE
# invocation, with v1 and v2 NON-CONSECUTIVE (paired per round, not two big
# blocks), then aggregates and re-plots. Designed to be launched once and left
# unattended for hours/days — it is detached-friendly, checkpointed, and
# fault-tolerant, so a reboot or a transient gateway failure never loses the run.
#
#   nohup bash scripts/testbed/run_full_redo.sh > redo_$(date -u +%Y%m%d).log 2>&1 &
#
# It does NOT depend on this terminal or any editor session staying open — only on
# the testbed host and gateways staying powered. If the box reboots mid-run, just
# relaunch the SAME command: completed (round,batch,version) units are skipped and
# it resumes where it left off.
#
# Why rounds: run_batch.py fixes the library version per invocation (compile-time
# DEFAULT_ENV) and can't interleave. One ROUND = each batch run once under v1 then
# once under v2 (minutes apart => weather-matched pair); the ROUNDS reps are spread
# across passes => the 95% CI reflects real run-to-run variation, and v1/v2 are
# never one long block each. Switching version adds NO extra flashing (each SF cell
# reflashes anyway; the version switch rides on that reflash). Keep the build cache
# warm (never `pio run -t clean`) so each reflash is ~1-2 min.
#
# Env knobs:
#   ROUNDS=5              number of reps per cell per version (the CI sample size)
#   BATCHES="a b"         batch names/paths (default: the two redo batches)
#   V1_ENV / V2_ENV       PlatformIO envs (default ttgo-t-beam / ttgo-t-beam-v2)
#   INTER_ROUND_SLEEP=0   seconds to sleep between rounds (raise to spread rounds
#                         over hours/days for more weather variation; 0=continuous)
#   FIG_ROOT              figure output root (default docs/paper/figures)
#   SKIP_UPGRADE=0        1 => skip the one-time `deploy.sh upgrade`
#   SKIP_ANALYSIS=0       1 => collect data only, no multirun/plot at the end
#   EXTRA_RUN_ARGS        extra flags forwarded to run_batch.py
set -uo pipefail

ROUNDS="${ROUNDS:-5}"
V1_ENV="${V1_ENV:-ttgo-t-beam}"
V2_ENV="${V2_ENV:-ttgo-t-beam-v2}"
INTER_ROUND_SLEEP="${INTER_ROUND_SLEEP:-0}"
FIG_ROOT="${FIG_ROOT:-docs/paper/figures}"
SKIP_UPGRADE="${SKIP_UPGRADE:-0}"
SKIP_ANALYSIS="${SKIP_ANALYSIS:-0}"
EXTRA_RUN_ARGS="${EXTRA_RUN_ARGS:-}"
read -r -a BATCHES <<< "${BATCHES:-sim_load_compare_13node sim_load_compare_reach16}"

TESTBED_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$TESTBED_ROOT"
DEPLOY_SH="$TESTBED_ROOT/deploy.sh"
REPO_ROOT="$(cd "$TESTBED_ROOT/../.." && pwd)"
CKPT_DIR="runs/.redo_checkpoints"
mkdir -p "$CKPT_DIR"
OVERALL_RC=0

# Gateway repo path + branch (for the firmware-verification preflight). Read from
# testbed.conf so we don't hardcode; the commit every gateway MUST be at defaults
# to the local HEAD (override with EXPECTED_SHA=... e.g. to pin a specific commit).
REPO_PATH="/home/lora/LoRaChat"; GIT_BRANCH="new_loramesher"
# shellcheck disable=SC1091
[[ -f "$TESTBED_ROOT/testbed.conf" ]] && source "$TESTBED_ROOT/testbed.conf" 2>/dev/null || true
EXPECTED_SHA="${EXPECTED_SHA:-$(git -C "$REPO_ROOT" rev-parse HEAD 2>/dev/null)}"

ts()     { date -u +%Y%m%dT%H%M%SZ; }
log()    { printf '%s  %s\n' "$(ts)" "$*"; }
banner() { echo; echo "============================================================"; log "$*"; echo "============================================================"; }

# --- helpers copied from run_compare.sh conventions --------------------------
resolve_yaml() {
  local a="$1" base; base="$(basename "$a")"; base="${base%.yaml}"
  [[ -f "batches/${base}.yaml" ]] && { echo "batches/${base}.yaml"; return 0; }
  [[ -f "$a" ]] && { echo "$a"; return 0; }
  return 1
}
batch_name_of() {
  python3 - "$1" <<'PY'
import sys; sys.path.insert(0, ".")
from pathlib import Path
from runner.batch import load_batch
print(load_batch(Path(sys.argv[1])).name)
PY
}
est_minutes_of() {
  python3 - "$1" <<'PY'
import sys; sys.path.insert(0, ".")
from pathlib import Path
from runner.batch import load_batch
b = load_batch(Path(sys.argv[1])); tot = 0.0
for c in b.cells:
    wu = c.warmup_min   if c.warmup_min   is not None else b.warmup_min
    du = c.duration_min if c.duration_min is not None else b.duration_min
    cd = c.cooldown_min if c.cooldown_min is not None else b.cooldown_min
    tot += wu + du + cd
print(int(round(tot)))
PY
}

# Move every run dir produced by one `run_batch --runs 1` (runs/<name>/*-r01)
# into runs/<name>__<tag>/, renaming the trailing -r01 to -r0<round> so multirun
# sees each round as a distinct rep.
collect_round() {
  local name="$1" tag="$2" round="$3"
  local src="runs/${name}" dst="runs/${name}__${tag}"
  mkdir -p "$dst"
  local moved=0 d base new
  shopt -s nullglob
  for d in "$src"/*/; do
    d="${d%/}"; base="$(basename "$d")"
    new="${base%-r[0-9][0-9]}-r$(printf '%02d' "$round")"
    mv "$d" "$dst/$new" && moved=$((moved+1))
  done
  shopt -u nullglob
  rmdir "$src" 2>/dev/null || true
  log "collected $moved run(s) -> $dst (round $round)"
}

# Run one (batch, version) unit for one round: skip if checkpointed, else flash+
# run, collect, and checkpoint. A non-zero rc is logged, not fatal — the missing
# cell simply gets fewer reps and is retried in later rounds.
run_unit() {
  local yaml="$1" name="$2" env="$3" tag="$4" round="$5"
  local ck="$CKPT_DIR/${name}__${tag}.r$(printf '%02d' "$round").done"
  if [[ -f "$ck" ]]; then
    log "skip (done): $name [$tag] round $round"; return 0
  fi
  # Clear any stale intermediate dir from a previous interrupted attempt.
  [[ -d "runs/${name}" ]] && { mv "runs/${name}" "runs/${name}.stale-$(ts)" 2>/dev/null || rm -rf "runs/${name}"; }

  local extra="$EXTRA_RUN_ARGS --skip-upgrade"   # upgrade done once, up front
  banner "round $round/$ROUNDS — $name [$tag] (env $env)"
  log "DEFAULT_ENV=$env runner/run_batch.py $yaml --runs 1 $extra"
  DEFAULT_ENV="$env" python3 runner/run_batch.py "$yaml" --runs 1 $extra
  local rc=$?
  [[ $rc -ne 0 ]] && { OVERALL_RC=1; log "WARNING: $name [$tag] round $round rc=$rc — continuing (cell(s) retried next round)"; }

  collect_round "$name" "$tag" "$round"
  touch "$ck"
}

analyse() {
  local yaml="$1" name="$2"
  local v1="runs/${name}__lmv1" v2="runs/${name}__lmv2"
  [[ "$SKIP_ANALYSIS" == "1" ]] && { log "SKIP_ANALYSIS=1 — leaving $v1 / $v2 unaggregated"; return; }
  [[ -d "$v1" && -d "$v2" ]] || { log "skip analysis for $name — missing $v1 or $v2"; return; }
  banner "$name — aggregate + plot v1 vs v2"
  python3 analysis/multirun.py "$v1" || { OVERALL_RC=1; log "multirun failed on $v1"; }
  python3 analysis/multirun.py "$v2" || { OVERALL_RC=1; log "multirun failed on $v2"; }
  local topo=(--nodes 13 --max-hops 3); case "$name" in *reach16*|*full*) topo=(--nodes 16 --max-hops 5) ;; esac
  python3 analysis/plot_compare.py --v1 "$v1" --v2 "$v2" --out "${FIG_ROOT}/${name}" "${topo[@]}" \
    && log "figures -> ${FIG_ROOT}/${name}" || { OVERALL_RC=1; log "plot_compare failed for $name"; }
  # Independent raw-log audit + long-link health for the reach batch.
  python3 analysis/audit_app_pdr.py --v1 "$v1" --v2 "$v2" --out "${FIG_ROOT}/${name}" 2>/dev/null \
    && log "audit -> ${FIG_ROOT}/${name}" || log "audit skipped/failed for $name (non-fatal)"
  case "$name" in *reach16*|*full*)
    python3 analysis/link_health_corpus.py "$v1" "$v2" 2>/dev/null \
      && log "link-health corpus computed for $name" || log "link-health step skipped for $name (check args)";;
  esac
}

# --- resolve + validate up front so a typo fails fast, not hours in ----------
YAMLS=(); NAMES=()
for a in "${BATCHES[@]}"; do
  y="$(resolve_yaml "$a")" || { echo "error: no batch YAML for '$a' (looked in batches/)"; exit 2; }
  n="$(batch_name_of "$y")" || { echo "error: could not load batch '$y'"; exit 2; }
  YAMLS+=("$y"); NAMES+=("$n")
done

banner "v1-vs-v2 full redo — $(date -u) — ROUNDS=$ROUNDS"
log "v1 env: $V1_ENV    v2 env: $V2_ENV    batches: ${NAMES[*]}"
grand=0
for i in "${!YAMLS[@]}"; do
  m="$(est_minutes_of "${YAMLS[$i]}")"; per=$(( m * 2 * ROUNDS ))
  grand=$(( grand + per ))
  log "batch ${NAMES[$i]}: ~${m} min/pass x2 versions x${ROUNDS} rounds = ~${per} min (~$(( per/60 )) h)"
done
log "estimated device-time (excl. per-cell flash overhead): ~${grand} min (~$(( grand/60 )) h)"

# One-time gateway upgrade, then every inner run_batch uses --skip-upgrade.
if [[ "$SKIP_UPGRADE" != "1" ]]; then
  banner "one-time: deploy.sh upgrade (git pull origin/$GIT_BRANCH + pio pkg update)"
  # STRICT: a gateway that fails to sync would silently run stale firmware and
  # poison the whole campaign, so abort rather than continue. The commit-verify
  # below is the real safety net if `git pull` lands but doesn't reach HEAD.
  if ! bash "$DEPLOY_SH" upgrade; then
    log "FATAL: deploy.sh upgrade failed on >=1 gateway — aborting to avoid stale firmware."; exit 1
  fi
  # VERIFY every gateway actually reached the expected commit (proof, not hope).
  # merge-base --is-ancestor returns non-zero on any gateway missing EXPECTED_SHA,
  # and run_parallel propagates that, so a bad gateway aborts here.
  if [[ -n "$EXPECTED_SHA" ]]; then
    banner "verify every gateway is at/after $EXPECTED_SHA (fixed firmware present)"
    if ! bash "$DEPLOY_SH" run-remote "cd $REPO_PATH && git merge-base --is-ancestor $EXPECTED_SHA HEAD"; then
      log "FATAL: >=1 gateway is NOT at/after $EXPECTED_SHA — the fix is not on it."
      log "       Did you 'git push origin $GIT_BRANCH'? Gateways pull origin, not your local commits."; exit 1
    fi
    log "OK: all gateways verified at/after $EXPECTED_SHA"
  else
    log "WARNING: EXPECTED_SHA empty (not a git checkout?) — skipping commit verification."
  fi
fi

# Rounds x batches x versions, interleaved.
for r in $(seq 1 "$ROUNDS"); do
  banner "===== ROUND $r / $ROUNDS ====="
  for i in "${!YAMLS[@]}"; do
    run_unit "${YAMLS[$i]}" "${NAMES[$i]}" "$V1_ENV" lmv1 "$r"
    run_unit "${YAMLS[$i]}" "${NAMES[$i]}" "$V2_ENV" lmv2 "$r"
  done
  if [[ "$r" -lt "$ROUNDS" && "$INTER_ROUND_SLEEP" -gt 0 ]]; then
    log "inter-round sleep ${INTER_ROUND_SLEEP}s"; sleep "$INTER_ROUND_SLEEP"
  fi
done

# Aggregate + plot once all rounds are in.
for i in "${!YAMLS[@]}"; do analyse "${YAMLS[$i]}" "${NAMES[$i]}"; done

banner "full redo done — overall rc=$OVERALL_RC"
[[ $OVERALL_RC -eq 0 ]] && log "all units completed; figures under ${FIG_ROOT}/<batch>/" \
                        || log "completed with warnings — grep this log for WARNING/ERROR (checkpoints in $CKPT_DIR let you relaunch to fill gaps)"
exit $OVERALL_RC
