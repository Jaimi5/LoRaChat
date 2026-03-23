#!/usr/bin/env bash
# deploy.sh - Testbed deployment orchestrator
# Runs from your local machine, SSHes into gateway machines to operate on devices.
#
# Usage: ./deploy.sh COMMAND [OPTIONS]
#
# Commands:
#   status        Check which gateways are reachable
#   upgrade       Git pull + pio pkg update on all gateways
#   upload        Compile and upload firmware to devices
#   monitor       Start serial monitors on all devices
#   stop-monitor  Stop all running monitors
#   logs          Collect log files from gateways to local machine
#   all           Run upgrade + upload + monitor in sequence
#
# Options:
#   -e ENV        Override PlatformIO environment
#   -g GW-1,GW-3  Limit to specific gateways
#   -d DEVICE_ID  Limit to specific device(s) (comma-separated)
#   -n SESSION    Session/experiment name (for log directories)
#   --skip-compile  Skip compilation, only upload
#   -c FILE       Path to testbed.conf (default: same dir as this script)
#   -h            Show this help

set -uo pipefail

# ── Resolve script directory and load config ────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ANSI colors for gateway output
declare -A GW_COLORS=(
    [GW-1]="\033[31m"   # Red
    [GW-2]="\033[32m"   # Green
    [GW-3]="\033[33m"   # Yellow
    [GW-4]="\033[34m"   # Blue
    [GW-5]="\033[35m"   # Magenta
    [GW-6]="\033[36m"   # Cyan
    [GW-7]="\033[37m"   # White
    [GW-8]="\033[91m"   # Bright red
)
RST="\033[0m"
BOLD="\033[1m"

# ── Parse arguments ─────────────────────────────────────────────────────────

COMMAND=""
OPT_ENV=""
OPT_GW=""
OPT_DEVICE=""
OPT_SESSION=""
OPT_SKIP_COMPILE=""
OPT_MONITOR=""
OPT_REMOTE_CMD=""
CONFIG_FILE="$SCRIPT_DIR/testbed.conf"

usage() {
    head -27 "${BASH_SOURCE[0]}" | tail -25 | sed 's/^# \?//'
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        status|upgrade|upload|monitor|stop-monitor|logs|clean|all)
            COMMAND="$1" ;;
        upload-monitor)
            COMMAND="upload"; OPT_MONITOR="1" ;;
        run-remote)
            COMMAND="run-remote" ;;
        -e) OPT_ENV="$2"; shift ;;
        -g) OPT_GW="$2"; shift ;;
        -d) OPT_DEVICE="$2"; shift ;;
        -n) OPT_SESSION="$2"; shift ;;
        --skip-compile) OPT_SKIP_COMPILE="--skip-compile" ;;
        --monitor) OPT_MONITOR="1" ;;
        -c) CONFIG_FILE="$2"; shift ;;
        -h|--help) usage 0 ;;
        *)
            if [[ "$COMMAND" == "run-remote" ]]; then
                OPT_REMOTE_CMD="$1"
            else
                echo "Unknown argument: $1"; usage 1
            fi
            ;;
    esac
    shift
done

if [[ -z "$COMMAND" ]]; then
    echo "Error: no command specified."
    usage 1
fi

# ── Load config ──────────────────────────────────────────────────────────────

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Error: config file not found: $CONFIG_FILE"
    exit 1
fi
source "$CONFIG_FILE"

ENV="${OPT_ENV:-$DEFAULT_ENV}"
SESSION="${OPT_SESSION:-deploy-$(date +%Y%m%d-%H%M%S)}"

# ── Build per-gateway device lists ───────────────────────────────────────────

# Filter gateways if -g specified
declare -a ACTIVE_GWS=()
if [[ -n "$OPT_GW" ]]; then
    IFS=',' read -ra ACTIVE_GWS <<< "$OPT_GW"
else
    ACTIVE_GWS=("${!GW_SSH[@]}")
fi

# Sort gateway list for consistent output
IFS=$'\n' ACTIVE_GWS=($(sort <<<"${ACTIVE_GWS[*]}")); unset IFS

# Build device lists per gateway
# GW_DEVICES[GW-1]="DEV1:PORT1 DEV2:PORT2 ..."
declare -A GW_DEVICES=()

for entry in "${DEVICES[@]}"; do
    IFS=':' read -r dev_id gw_id <<< "$entry"

    # Auto-generate serial port from device ID
    # e.g., C6E104-77A4 → /home/lora/dev/lora-77A4
    short_id="${dev_id##*-}"
    port="${SERIAL_PORT_PREFIX}${short_id}"

    # Skip gateways not in active list
    local_match=0
    for agw in "${ACTIVE_GWS[@]}"; do
        [[ "$agw" == "$gw_id" ]] && local_match=1 && break
    done
    [[ $local_match -eq 0 ]] && continue

    # Filter by device if -d specified
    if [[ -n "$OPT_DEVICE" ]]; then
        device_match=0
        IFS=',' read -ra dev_filter <<< "$OPT_DEVICE"
        for df in "${dev_filter[@]}"; do
            [[ "$dev_id" == "$df" ]] && device_match=1 && break
        done
        [[ $device_match -eq 0 ]] && continue
    fi

    if [[ -n "${GW_DEVICES[$gw_id]+x}" ]]; then
        GW_DEVICES[$gw_id]+=" $dev_id:$port"
    else
        GW_DEVICES[$gw_id]="$dev_id:$port"
    fi
done

# ── Helper functions ─────────────────────────────────────────────────────────

log_gw() {
    local gw_id="$1"; shift
    local color="${GW_COLORS[$gw_id]:-\033[0m}"
    echo -e "${color}[${gw_id}]${RST} $*"
}

# Build SSH command prefix for a gateway (handles sshpass if password is set)
# Usage: ssh_prefix GW_ID  → outputs "sshpass -p pass ssh OPTS" or "ssh OPTS"
ssh_prefix() {
    local gw_id="$1"
    local pass="${GW_PASS[$gw_id]:-}"
    if [[ -n "$pass" ]]; then
        if ! command -v sshpass &>/dev/null; then
            echo "ERROR: sshpass not installed. Install with: apt install sshpass" >&2
            return 1
        fi
        # shellcheck disable=SC2086
        echo "sshpass -p '$pass' ssh $SSH_OPTS"
    else
        # No password: add BatchMode to prevent hanging on prompts
        # shellcheck disable=SC2086
        echo "ssh $SSH_OPTS -o BatchMode=yes"
    fi
}

# Build SCP command prefix for a gateway (handles sshpass if password is set)
scp_prefix() {
    local gw_id="$1"
    local pass="${GW_PASS[$gw_id]:-}"
    if [[ -n "$pass" ]]; then
        # shellcheck disable=SC2086
        echo "sshpass -p '$pass' scp $SSH_OPTS"
    else
        # shellcheck disable=SC2086
        echo "scp $SSH_OPTS -o BatchMode=yes"
    fi
}

# Run a command on a gateway via SSH (with retries)
# Usage: ssh_gw GW_ID "remote command"
ssh_gw() {
    local gw_id="$1"
    local cmd="$2"
    local ssh_dest="${GW_SSH[$gw_id]}"
    local pass="${GW_PASS[$gw_id]:-}"
    local retries="${SSH_RETRIES:-3}"
    local attempt=1
    local full_cmd="export PATH=$PIO_PATH:\$PATH; $cmd"

    while [[ $attempt -le $retries ]]; do
        if [[ -n "$pass" ]]; then
            # shellcheck disable=SC2086
            SSHPASS="$pass" sshpass -e ssh $SSH_OPTS "$ssh_dest" "$full_cmd" && return 0
        else
            # shellcheck disable=SC2086
            ssh $SSH_OPTS -o BatchMode=yes "$ssh_dest" "$full_cmd" && return 0
        fi
        local rc=$?
        if [[ $attempt -lt $retries ]]; then
            echo "SSH to $gw_id failed (attempt $attempt/$retries), retrying in 5s..." >&2
            sleep 5
        fi
        attempt=$((attempt + 1))
    done
    return $rc
}

# Run SSH commands on multiple gateways in parallel with spinner display
# Output goes to log files, console shows clean progress spinners
# Usage: run_parallel LABEL GW_ID1 "cmd1" GW_ID2 "cmd2" ...
#   LABEL is used for log file naming (e.g., "upgrade", "upload")
run_parallel() {
    local label="$1"; shift
    local -A pids=()
    local -A logfiles=()
    local -A start_times=()
    local -A exit_codes=()
    local -A finish_times=()
    local -a gw_order=()
    local log_dir="$LOCAL_LOG_DIR/$SESSION"
    mkdir -p "$log_dir"

    local spinner_chars='|/-\'
    local spinner_len=${#spinner_chars}

    # Launch all SSH sessions
    while [[ $# -ge 2 ]]; do
        local gw_id="$1"
        local cmd="$2"
        shift 2

        local logfile="$log_dir/${gw_id}-${label}.log"
        logfiles[$gw_id]="$logfile"
        start_times[$gw_id]=$SECONDS
        gw_order+=("$gw_id")

        ssh_gw "$gw_id" "$cmd" > "$logfile" 2>&1 &
        pids[$gw_id]=$!
    done

    local total=${#gw_order[@]}
    if [[ $total -eq 0 ]]; then
        echo "No gateways to process."
        return 1
    fi

    # Print initial status lines
    for gw_id in "${gw_order[@]}"; do
        echo ""
    done

    # Hide cursor during spinner, restore on exit or interrupt
    tput civis 2>/dev/null
    trap 'tput cnorm 2>/dev/null' EXIT INT TERM

    # Spinner display loop
    local tick=0
    local running=1
    while [[ $running -gt 0 ]]; do
        running=0

        # Move cursor up to redraw
        echo -ne "\033[${total}A"

        for gw_id in "${gw_order[@]}"; do
            local color="${GW_COLORS[$gw_id]:-\033[0m}"
            local elapsed=$(( SECONDS - start_times[$gw_id] ))
            local time_str="${elapsed}s"

            if [[ -z "${exit_codes[$gw_id]+x}" ]]; then
                # Still running — check if finished
                if kill -0 "${pids[$gw_id]}" 2>/dev/null; then
                    running=$((running + 1))
                    local si=$(( tick % spinner_len ))
                    local sc="${spinner_chars:$si:1}"
                    printf "\033[2K  ${color}%-6s${RST} ${sc} Running...    (%ss)\n" "$gw_id" "$elapsed"
                else
                    # Just finished — freeze the elapsed time
                    wait "${pids[$gw_id]}" 2>/dev/null
                    exit_codes[$gw_id]=$?
                    finish_times[$gw_id]="$elapsed"
                    if [[ ${exit_codes[$gw_id]} -eq 0 ]]; then
                        printf "\033[2K  ${color}%-6s${RST} \033[32m✓ OK\033[0m            (%ss)\n" "$gw_id" "${finish_times[$gw_id]}"
                    else
                        printf "\033[2K  ${color}%-6s${RST} \033[31m✗ FAIL\033[0m          (%ss)  → %s\n" "$gw_id" "${finish_times[$gw_id]}" "${logfiles[$gw_id]}"
                    fi
                fi
            else
                # Already finished — reprint with frozen time
                if [[ ${exit_codes[$gw_id]} -eq 0 ]]; then
                    printf "\033[2K  ${color}%-6s${RST} \033[32m✓ OK\033[0m            (%ss)\n" "$gw_id" "${finish_times[$gw_id]}"
                else
                    printf "\033[2K  ${color}%-6s${RST} \033[31m✗ FAIL\033[0m          (%ss)  → %s\n" "$gw_id" "${finish_times[$gw_id]}" "${logfiles[$gw_id]}"
                fi
            fi
        done

        tick=$((tick + 1))
        [[ $running -gt 0 ]] && sleep 0.3
    done

    # Restore cursor
    tput cnorm 2>/dev/null
    trap - EXIT INT TERM

    # Final summary
    echo ""
    local failed=0
    local succeeded=0
    for gw_id in "${gw_order[@]}"; do
        if [[ ${exit_codes[$gw_id]} -ne 0 ]]; then
            failed=$((failed + 1))
        else
            succeeded=$((succeeded + 1))
        fi
    done

    echo -e "${BOLD}Result: ${succeeded}/${total} OK${RST}"
    if [[ $failed -gt 0 ]]; then
        echo -e "Logs: ${log_dir}/"
    fi
    echo ""

    [[ $failed -eq 0 ]]
}

# ── Commands ─────────────────────────────────────────────────────────────────

cmd_status() {
    echo -e "${BOLD}Checking gateway connectivity...${RST}"
    echo ""

    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        args+=("$gw_id" "echo 'reachable'; hostname; uptime; echo '--- pio processes ---'; pgrep -a pio || echo 'No pio processes running'")
    done

    if [[ ${#args[@]} -eq 0 ]]; then
        echo "No gateways to check."
        return 1
    fi

    run_parallel "status" "${args[@]}"
}

cmd_upgrade() {
    echo -e "${BOLD}Upgrading all gateways (branch: $GIT_BRANCH)...${RST}"
    echo ""

    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        # Inline git commands (not gw-upgrade.sh) so it works even on first run
        # before the testbed scripts exist on the gateway
        args+=("$gw_id" "cd $REPO_PATH && echo '=== git fetch ===' && git fetch origin && echo '=== git checkout $GIT_BRANCH ===' && git checkout $GIT_BRANCH && echo '=== git pull ===' && git pull -X theirs origin $GIT_BRANCH && echo '=== pio pkg update ===' && pio pkg update && echo '=== Upgrade complete ==='")
    done

    if [[ ${#args[@]} -eq 0 ]]; then
        echo "No gateways to upgrade."
        return 1
    fi

    run_parallel "upgrade" "${args[@]}"
}

cmd_upload() {
    local monitor_flag=""
    if [[ -n "$OPT_MONITOR" ]]; then
        monitor_flag="--monitor $SESSION"
        echo -e "${BOLD}Compiling, uploading and monitoring (env: $ENV, session: $SESSION)...${RST}"
    else
        echo -e "${BOLD}Compiling and uploading (env: $ENV, session: $SESSION)...${RST}"
    fi
    echo ""

    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        local devices="${GW_DEVICES[$gw_id]:-}"
        [[ -z "$devices" ]] && continue
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue

        local cmd="cd $REPO_PATH && bash scripts/testbed/gw-upload.sh $ENV $OPT_SKIP_COMPILE $monitor_flag $devices"
        args+=("$gw_id" "$cmd")
    done

    if [[ ${#args[@]} -eq 0 ]]; then
        echo "No devices to upload. Check -g/-d filters and testbed.conf."
        return 1
    fi

    run_parallel "upload" "${args[@]}"
}

cmd_monitor() {
    echo -e "${BOLD}Starting monitors (session: $SESSION)...${RST}"
    echo ""

    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        local devices="${GW_DEVICES[$gw_id]:-}"
        [[ -z "$devices" ]] && continue
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue

        local cmd="cd $REPO_PATH && bash scripts/testbed/gw-monitor.sh start $SESSION $ENV $devices"
        args+=("$gw_id" "$cmd")
    done

    if [[ ${#args[@]} -eq 0 ]]; then
        echo "No devices to monitor. Check -g/-d filters and testbed.conf."
        return 1
    fi

    run_parallel "monitor" "${args[@]}"
    echo ""
    echo "Monitors running in background on gateways."
    echo "Use './deploy.sh logs -n $SESSION' to collect logs."
    echo "Use './deploy.sh stop-monitor' to stop."
}

cmd_stop_monitor() {
    echo -e "${BOLD}Stopping all monitors...${RST}"
    echo ""

    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        args+=("$gw_id" "cd $REPO_PATH && bash scripts/testbed/gw-monitor.sh stop")
    done

    if [[ ${#args[@]} -eq 0 ]]; then
        echo "No gateways to stop."
        return 1
    fi

    run_parallel "stop-monitor" "${args[@]}"

    # Compress logs on gateways after stopping
    echo -e "${BOLD}Compressing logs on gateways...${RST}"
    local cargs=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        cargs+=("$gw_id" "find $REPO_PATH/logs/ -name '*.log' -size +0 -exec gzip -f {} \; 2>/dev/null; echo 'Compressed'")
    done
    [[ ${#cargs[@]} -gt 0 ]] && run_parallel "compress" "${cargs[@]}"
}

cmd_clean() {
    local days="${MAX_LOG_AGE_DAYS:-7}"
    echo -e "${BOLD}Cleaning logs older than ${days} days...${RST}"
    echo ""

    # Clean local logs
    local local_count=0
    if [[ -d "$LOCAL_LOG_DIR" ]]; then
        local_count=$(find "$LOCAL_LOG_DIR" -name "*.log" -o -name "*.log.gz" -mtime +"$days" 2>/dev/null | wc -l)
        if [[ $local_count -gt 0 ]]; then
            echo "Local: removing $local_count file(s) from $LOCAL_LOG_DIR/"
            find "$LOCAL_LOG_DIR" -name "*.log" -o -name "*.log.gz" -mtime +"$days" -delete 2>/dev/null
            # Remove empty session directories
            find "$LOCAL_LOG_DIR" -type d -empty -delete 2>/dev/null
        else
            echo "Local: no old logs found."
        fi
    fi

    # Clean gateway logs
    echo ""
    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        args+=("$gw_id" "count=\$(find $REPO_PATH/logs/ -name '*.log' -o -name '*.log.gz' -mtime +$days 2>/dev/null | wc -l); echo \"Found \$count old file(s)\"; find $REPO_PATH/logs/ -name '*.log' -o -name '*.log.gz' -mtime +$days -delete 2>/dev/null; find $REPO_PATH/logs/ -type d -empty -delete 2>/dev/null; echo 'Cleaned'")
    done

    if [[ ${#args[@]} -gt 0 ]]; then
        run_parallel "clean" "${args[@]}"
    fi
}

cmd_logs() {
    echo -e "${BOLD}Collecting logs for session: $SESSION${RST}"
    echo ""

    local local_dir="$LOCAL_LOG_DIR/$SESSION"
    mkdir -p "$local_dir"

    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        local ssh_dest="${GW_SSH[$gw_id]}"

        log_gw "$gw_id" "Fetching logs..."
        local scp_cmd
        scp_cmd=$(scp_prefix "$gw_id") || continue
        if eval "$scp_cmd" -r "$ssh_dest:$REPO_PATH/logs/$SESSION/*.log" "$local_dir/" 2>/dev/null; then
            local count
            count=$(ls -1 "$local_dir"/*.log 2>/dev/null | wc -l)
            log_gw "$gw_id" "OK: $count log file(s)"
        else
            log_gw "$gw_id" "No logs found for session $SESSION"
        fi
    done

    echo ""
    echo "Logs collected in: $local_dir"
    ls -la "$local_dir"/*.log 2>/dev/null || true
}

cmd_run_remote() {
    if [[ -z "$OPT_REMOTE_CMD" ]]; then
        echo "Error: no command specified. Usage: deploy.sh run-remote [-g GW-1] \"command\""
        return 1
    fi

    echo -e "${BOLD}Running on gateways: ${OPT_REMOTE_CMD}${RST}"
    echo ""

    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        args+=("$gw_id" "$OPT_REMOTE_CMD")
    done

    if [[ ${#args[@]} -eq 0 ]]; then
        echo "No gateways selected."
        return 1
    fi

    run_parallel "remote" "${args[@]}"

    echo ""
    echo -e "Logs: ${LOCAL_LOG_DIR}/${SESSION}/"
}

cmd_all() {
    echo -e "${BOLD}Full deployment: upgrade -> upload+monitor${RST}"
    echo -e "${BOLD}Session: $SESSION | Env: $ENV${RST}"
    echo ""

    echo -e "\n${BOLD}=== Phase 1/2: Upgrade ===${RST}\n"
    if ! cmd_upgrade; then
        echo ""
        echo "WARNING: Some gateways failed to upgrade. Continue anyway? (y/N)"
        read -r answer
        [[ "$answer" != "y" && "$answer" != "Y" ]] && exit 1
    fi

    echo -e "\n${BOLD}=== Phase 2/2: Upload + Monitor ===${RST}\n"
    OPT_MONITOR="1"
    cmd_upload
}

# ── Main ─────────────────────────────────────────────────────────────────────

case "$COMMAND" in
    status)       cmd_status ;;
    upgrade)      cmd_upgrade ;;
    upload)       cmd_upload ;;
    monitor)      cmd_monitor ;;
    stop-monitor) cmd_stop_monitor ;;
    logs)         cmd_logs ;;
    clean)        cmd_clean ;;
    run-remote)   cmd_run_remote ;;
    all)          cmd_all ;;
    *)            echo "Unknown command: $COMMAND"; usage 1 ;;
esac
