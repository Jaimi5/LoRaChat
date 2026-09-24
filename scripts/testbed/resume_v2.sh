#!/usr/bin/env bash
# resume_v2.sh — re-run ONLY the v2 env passes of a compare campaign whose v2 side
# failed (e.g. the <span> build break), leaving the intact *_v1 data untouched.
#
# Usage:
#   scripts/testbed/resume_v2.sh [BATCH ...]        # default: the two compare batches
#
# Env knobs:
#   V2_ENV        PlatformIO v2 env         (default: ttgo-t-beam-v2)
#   DO_UPGRADE    1 = let the first pass git-pull + pio pkg update the gateways
#                 (default 0: assumes the library cache was already refreshed;
#                  a plain pkg update may NOT move a stale branch dep, so prefer
#                  wiping .pio/libdeps/*/LoRaMesher on the gateways first).
#   RUN_ANALYSIS  1 = after both passes, aggregate + plot v1-vs-v2 (default 0).
#   FIG_ROOT      figure output root        (default: docs/paper/figures)
#   EXTRA_RUN_ARGS  extra flags forwarded to run_batch.py
set -uo pipefail

V2_ENV="${V2_ENV:-ttgo-t-beam-v2}"
DO_UPGRADE="${DO_UPGRADE:-0}"
RUN_ANALYSIS="${RUN_ANALYSIS:-0}"
EXTRA_RUN_ARGS="${EXTRA_RUN_ARGS:-}"

TESTBED_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$TESTBED_ROOT"

# Figures belong with the paper at the repo root, not under scripts/testbed —
# resolve to an absolute path so it's correct regardless of the cwd plot_compare
# runs in (a relative default would land in $TESTBED_ROOT/docs/paper/figures).
REPO_ROOT="$(cd "$TESTBED_ROOT/../.." && pwd)"
FIG_ROOT="${FIG_ROOT:-$REPO_ROOT/docs/paper/figures}"

OVERALL_RC=0
ts()   { date -u +%Y%m%dT%H%M%SZ; }
log()  { printf '%s  %s\n' "$(ts)" "$*"; }
banner() { echo; echo "============================================================"; log "$*"; echo "============================================================"; }

resolve_yaml() {
  local a="$1" base; base="$(basename "$a")"; base="${base%.yaml}"
  local cand="batches/${base}.yaml"
  [[ -f "$cand" ]] && { echo "$cand"; return 0; }
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

# UPGRADE_DONE gates the one-time gateway upgrade the same way run_compare.sh does.
UPGRADE_DONE=$(( DO_UPGRADE ? 0 : 1 ))

run_v2_pass() {
  local yaml="$1" name="$2"
  local src="runs/${name}" dst="runs/${name}_v2"

  [[ -d "$src" ]] && mv "$src" "${src}.stale-$(ts)" && log "moved stale $src aside"
  if [[ -d "$dst" ]] && [[ -n "$(ls -A "$dst" 2>/dev/null)" ]]; then
    mv "$dst" "${dst}.failed-$(ts)"; log "existing $dst backed up (failed-*)"
  fi

  local extra="$EXTRA_RUN_ARGS"
  [[ "$UPGRADE_DONE" == "1" ]] && extra="$extra --skip-upgrade"

  banner "$name [v2] — flashing $V2_ENV"
  log "DEFAULT_ENV=$V2_ENV runner/run_batch.py $yaml $extra"
  DEFAULT_ENV="$V2_ENV" python3 runner/run_batch.py "$yaml" $extra
  local rc=$?
  UPGRADE_DONE=1
  [[ $rc -ne 0 ]] && { OVERALL_RC=1; log "WARNING: run_batch for $name [v2] rc=$rc; continuing"; }

  # DrvFs/WSL can hold a Windows-side rename lock on a just-written dir, so a
  # plain `mv` fails with EPERM even though the tree is readable. Fall back to a
  # copy and best-effort clear of the original.
  if [[ -d "$src" ]]; then
    if mv "$src" "$dst" 2>/dev/null; then
      log "collected v2 data -> $dst"
    elif cp -r "$src" "$dst"; then
      rm -rf "$src" 2>/dev/null || mv "$src" "${src}.orphan-$(ts)" 2>/dev/null \
        || log "WARN: copied to $dst but could not clear locked $src (delete it manually)"
      log "collected v2 data -> $dst (via copy; rename was blocked)"
    else
      log "ERROR: could not collect $src -> $dst"; OVERALL_RC=1
    fi
  else log "ERROR: expected output $src not found after $name [v2]"; OVERALL_RC=1; fi
}

analyse() {
  # NB: keep `name` on its own `local` — bash expands ${name} while building the
  # `local` arg list, so a combined `local name=$1 v1=...${name}...` dies under
  # `set -u` with "name: unbound variable" before the assignment takes effect.
  local name="$1"
  local v1="runs/${name}_v1" v2="runs/${name}_v2"
  [[ -d "$v1" && -d "$v2" ]] || { log "skip analysis for $name — missing $v1 or $v2"; return; }
  banner "$name — aggregate + plot v1 vs v2"
  python3 analysis/multirun.py "$v1" || { OVERALL_RC=1; log "multirun failed on $v1"; }
  python3 analysis/multirun.py "$v2" || { OVERALL_RC=1; log "multirun failed on $v2"; }
  local xarg=""  ; case "$name" in *size*) xarg="--x size" ;; esac
  local topo=(--nodes 13 --max-hops 3); case "$name" in *full*) topo=(--nodes 16 --max-hops 5) ;; esac
  local out="${FIG_ROOT}/${name}"
  python3 analysis/plot_compare.py --v1 "$v1" --v2 "$v2" --out "$out" "${topo[@]}" $xarg \
    && log "figures -> $out" || { OVERALL_RC=1; log "plot_compare failed for $name"; }
}

main() {
  local args=("$@")
  [[ ${#args[@]} -eq 0 ]] && args=(sim_size_compare sim_load_compare_full)

  local yamls=() names=() y n
  for a in "${args[@]}"; do
    y="$(resolve_yaml "$a")" || { echo "error: no batch YAML for '$a'"; exit 2; }
    n="$(batch_name_of "$y")" || { echo "error: could not load batch '$y'"; exit 2; }
    yamls+=("$y"); names+=("$n")
  done

  banner "v2 resume — $(date -u)   env=$V2_ENV   do_upgrade=$DO_UPGRADE"
  for i in "${!yamls[@]}"; do run_v2_pass "${yamls[$i]}" "${names[$i]}"; done
  [[ "$RUN_ANALYSIS" == "1" ]] && for n in "${names[@]}"; do analyse "$n"; done

  banner "v2 resume done — overall rc=$OVERALL_RC"
  return $OVERALL_RC
}
main "$@"
