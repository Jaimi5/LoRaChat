#!/usr/bin/env bash
# soak_update.sh - Refresh the soak report from the PC. The gateways run the soak
# unattended (looped monitors, @reboot resume, daily rotation in gw-monitor.sh);
# the PC is only needed while this runs, and after days off it catches up all the
# missing day files in one call.
#
# Usage: bash soak_update.sh [SESSION] [--no-snapshot]
#
# 1. deploy.sh logs-days fetches the *-YYYYMMDD.log.gz files not here yet, plus a
#    snapshot of today's live logs (unless --no-snapshot).
# 2. analysis/soak_report.py -> logs_testbed/<SESSION>_report/soak.json
#    (the old-library baseline full_deploy_sf10_v3 is computed once and cached).
# 3. analysis/soak_page.py renders logs_testbed/<SESSION>_report/<SESSION>.html,
#    which is republished to the same artifact URL. A hand-written
#    logs_testbed/<SESSION>_report/notes.html, if present, becomes its findings panel.

set -uo pipefail

SESSION="soak_200_sf10"
SNAPSHOT="--snapshot"
for arg in "$@"; do
    case "$arg" in
        --no-snapshot) SNAPSHOT="" ;;
        -*) echo "Unknown option: $arg"; exit 1 ;;
        *) SESSION="$arg" ;;
    esac
done

cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
export SSH_AUTH_SOCK="${SSH_AUTH_SOCK:-$HOME/.ssh/lm200-agent.sock}"

LOG_DIR="logs_testbed/$SESSION"
OUT="logs_testbed/${SESSION}_report"
BASELINE_DIR="${BASELINE_DIR:-../../../LoRaChat/logs_testbed/full_deploy_sf10_v3}"
BASELINE_JSON="$OUT/baseline_$(basename "$BASELINE_DIR").json"
mkdir -p "$OUT"

echo "=== Fetching day files ($SESSION) ==="
fetch_rc=0
bash deploy.sh logs-days -n "$SESSION" $SNAPSHOT || fetch_rc=$?
[[ $fetch_rc -ne 0 ]] && echo "WARNING: some gateways failed; the report covers what is here."

if [[ ! -f "$BASELINE_JSON" && -d "$BASELINE_DIR" ]]; then
    echo "=== Baseline $(basename "$BASELINE_DIR") (once) ==="
    python3 analysis/soak_report.py "$BASELINE_DIR" --json "$BASELINE_JSON" -q || exit 1
fi

echo "=== Report ==="
python3 analysis/soak_report.py "$LOG_DIR" --json "$OUT/soak.json" || exit 1

page_args=("$OUT/soak.json" -o "$OUT/$SESSION.html")
[[ -f "$BASELINE_JSON" ]] && page_args+=(--baseline "$BASELINE_JSON")
[[ -f "$OUT/notes.html" ]] && page_args+=(--notes "$OUT/notes.html")  # hand-written findings
python3 analysis/soak_page.py "${page_args[@]}" || exit 1
echo ""
echo "Page: $(pwd)/$OUT/$SESSION.html"
exit $fetch_rc
