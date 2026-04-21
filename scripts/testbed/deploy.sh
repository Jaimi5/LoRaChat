#!/usr/bin/env bash
# deploy.sh - Testbed deployment orchestrator
# Runs from your local machine, SSHes into gateway machines to operate on devices.
#
# Usage: ./deploy.sh COMMAND [OPTIONS]
#
# Commands:
#   status        Check which gateways are reachable
#   upgrade       Git pull + pio pkg update on all gateways
#   push-config   Generate change-config-*.sh from an experiment YAML and scp per-GW
#   upload        Compile and upload firmware to devices (runs push-config first)
#   monitor       Start serial monitors on all devices
#   stop-monitor  Stop all running monitors
#   logs          Collect log files from gateways to local machine
#   all           Run stop-monitor + upgrade + push-config + upload+monitor in sequence
#
# Options:
#   -e ENV        Override PlatformIO environment
#   -g GW-1,GW-3  Limit to specific gateways
#   -d DEVICE_ID  Limit to specific device(s) (comma-separated)
#   -n SESSION    Session/experiment name (for log directories)
#   -x YAML       Experiment YAML (default: experiments/current.yaml if it exists)
#   --skip-compile  Skip compilation, only upload
#   --skip-config   Skip the push-config step (use whatever's on the gateway)
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
OPT_SKIP_CONFIG=""
OPT_EXPERIMENT_YAML=""
OPT_MONITOR=""
OPT_REMOTE_CMD=""
CONFIG_FILE="$SCRIPT_DIR/testbed.conf"

usage() {
    # Print the leading comment block (stops at the first non-comment line).
    awk 'NR>1 && /^[^#]/ {exit} NR>1 {sub(/^# ?/, ""); print}' "${BASH_SOURCE[0]}"
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        status|upgrade|upload|monitor|stop-monitor|logs|clean|sync-time|all|push-config)
            COMMAND="$1" ;;
        upload-monitor)
            COMMAND="upload"; OPT_MONITOR="1" ;;
        run-remote)
            COMMAND="run-remote" ;;
        -e) OPT_ENV="$2"; shift ;;
        -g) OPT_GW="$2"; shift ;;
        -d) OPT_DEVICE="$2"; shift ;;
        -n) OPT_SESSION="$2"; shift ;;
        -x) OPT_EXPERIMENT_YAML="$2"; shift ;;
        --skip-compile) OPT_SKIP_COMPILE="--skip-compile" ;;
        --skip-config)  OPT_SKIP_CONFIG="1" ;;
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

# Resolve the experiment YAML for push-config. -x wins; otherwise fall back
# to experiments/current.yaml if it exists; otherwise empty (push-config
# no-ops and upload proceeds with whatever is already on the gateway).
if [[ -n "$OPT_EXPERIMENT_YAML" ]]; then
    EXP_YAML="$OPT_EXPERIMENT_YAML"
elif [[ -f "$SCRIPT_DIR/experiments/current.yaml" ]]; then
    EXP_YAML="$SCRIPT_DIR/experiments/current.yaml"
else
    EXP_YAML=""
fi

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

# Run SCP for a gateway (handles password, key, port)
# Usage: scp_gw GW_ID src dest
scp_gw() {
    local gw_id="$1"
    local src="$2"
    local dest="$3"
    local pass="${GW_PASS[$gw_id]:-}"
    local key="${GW_KEY[$gw_id]:-}"
    local port="${GW_PORT[$gw_id]:-}"

    # shellcheck disable=SC2086
    local -a scp_args=($SSH_OPTS)
    [[ -n "$key" ]] && scp_args+=(-i "$key")
    [[ -n "$port" ]] && scp_args+=(-P "$port")
    [[ -z "$pass" ]] && scp_args+=(-o BatchMode=yes)

    if [[ -n "$pass" ]]; then
        SSHPASS="$pass" sshpass -e scp "${scp_args[@]}" -r "$src" "$dest"
    else
        scp "${scp_args[@]}" -r "$src" "$dest"
    fi
}

# Like scp_gw but accepts multiple local sources and a single remote dest.
# Usage: scp_gw_multi GW_ID src1 src2 ... "remote:path"
scp_gw_multi() {
    local gw_id="$1"; shift
    if [[ $# -lt 2 ]]; then
        echo "scp_gw_multi: need at least one source and a remote dest" >&2
        return 2
    fi
    local dest_rel="${!#}"               # last positional = "remote-side path"
    local -a srcs=("${@:1:$#-1}")
    local pass="${GW_PASS[$gw_id]:-}"
    local key="${GW_KEY[$gw_id]:-}"
    local port="${GW_PORT[$gw_id]:-}"

    # shellcheck disable=SC2086
    local -a scp_args=($SSH_OPTS)
    [[ -n "$key" ]] && scp_args+=(-i "$key")
    [[ -n "$port" ]] && scp_args+=(-P "$port")
    [[ -z "$pass" ]] && scp_args+=(-o BatchMode=yes)

    local dest="${GW_SSH[$gw_id]}${dest_rel}"

    if [[ -n "$pass" ]]; then
        SSHPASS="$pass" sshpass -e scp "${scp_args[@]}" "${srcs[@]}" "$dest"
    else
        scp "${scp_args[@]}" "${srcs[@]}" "$dest"
    fi
}

# Run a command on a gateway via SSH (with retries)
# Usage: ssh_gw GW_ID "remote command"
ssh_gw() {
    local gw_id="$1"
    local cmd="$2"
    local ssh_dest="${GW_SSH[$gw_id]}"
    local pass="${GW_PASS[$gw_id]:-}"
    local key="${GW_KEY[$gw_id]:-}"
    local port="${GW_PORT[$gw_id]:-}"
    local retries="${SSH_RETRIES:-3}"
    local attempt=1
    local full_cmd="export PATH=$PIO_PATH:\$PATH; $cmd"

    # Build SSH args dynamically
    # shellcheck disable=SC2086
    local -a ssh_args=($SSH_OPTS)
    [[ -n "$key" ]] && ssh_args+=(-i "$key")
    [[ -n "$port" ]] && ssh_args+=(-p "$port")
    [[ -z "$pass" ]] && ssh_args+=(-o BatchMode=yes)

    while [[ $attempt -le $retries ]]; do
        if [[ -n "$pass" ]]; then
            SSHPASS="$pass" sshpass -e ssh "${ssh_args[@]}" "$ssh_dest" "$full_cmd" && return 0
        else
            ssh "${ssh_args[@]}" "$ssh_dest" "$full_cmd" && return 0
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

cmd_push_config() {
    if [[ -z "$EXP_YAML" ]]; then
        echo "No experiment YAML found (pass -x or create experiments/current.yaml)."
        echo "Skipping push-config; devices will use whatever is already on their gateway."
        return 0
    fi

    if [[ ! -f "$EXP_YAML" ]]; then
        echo "Error: experiment YAML not found: $EXP_YAML" >&2
        return 1
    fi

    echo -e "${BOLD}Push-config: $EXP_YAML${RST}"
    echo ""

    # 1. Validate (fail fast before touching any gateway).
    if ! python3 "$SCRIPT_DIR/configtool/configtool.py" validate \
            "$EXP_YAML" --strict-devices; then
        echo ""
        echo "Validation failed; aborting push-config." >&2
        return 1
    fi

    # 2. Generate locally into $SCRIPT_DIR/change-config/.
    if ! python3 "$SCRIPT_DIR/configtool/configtool.py" generate "$EXP_YAML" \
            >/dev/null; then
        echo "Error: configtool generate failed." >&2
        return 1
    fi

    # 3. Per-gateway: scp only the scripts for that GW's devices. Run in
    #    parallel via run_parallel, wrapping the scp in a helper child script
    #    that re-imports the testbed.conf and our helpers via `bash -c`.
    local local_cc_dir="$SCRIPT_DIR/change-config"
    local remote_cc_dir="$REPO_PATH/scripts/testbed/change-config"

    local args=()
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        local devices="${GW_DEVICES[$gw_id]:-}"
        [[ -z "$devices" ]] && continue

        # Collect the scripts we actually need to push to this gateway.
        local -a srcs=()
        for entry in $devices; do
            local dev_id="${entry%%:*}"
            local short_id="${dev_id##*-}"
            local script="$local_cc_dir/change-config-$short_id.sh"
            if [[ -f "$script" ]]; then
                srcs+=("$script")
            else
                echo "WARN: $script not generated (skipping $dev_id)" >&2
            fi
        done
        [[ ${#srcs[@]} -eq 0 ]] && continue

        # We run the mkdir+scp inside run_parallel by chaining them as a
        # single remote-plus-local operation. Since run_parallel only takes
        # SSH commands, do the mkdir via ssh_gw here and push files below.
        ssh_gw "$gw_id" "mkdir -p $remote_cc_dir && rm -f $remote_cc_dir/change-config-*.sh" \
            >/dev/null 2>&1 || {
                echo "ERROR: could not prepare $remote_cc_dir on $gw_id" >&2
                return 1
            }

        # Sequential scp per-GW (run_parallel is SSH-shaped, not scp-shaped).
        # Each GW only has 1-3 devices so this is fast. Do the gateways
        # themselves in the background to parallelise across GWs.
        (
            if scp_gw_multi "$gw_id" "${srcs[@]}" ":$remote_cc_dir/" >/dev/null 2>&1; then
                log_gw "$gw_id" "pushed ${#srcs[@]} script(s)"
            else
                log_gw "$gw_id" "SCP FAILED"
                exit 1
            fi
        ) &
        args+=("$!:$gw_id")
    done

    # Wait for all background scps and collect results.
    local failed=0
    for spec in "${args[@]}"; do
        local pid="${spec%%:*}"
        local gw="${spec#*:}"
        if ! wait "$pid"; then
            failed=$((failed + 1))
            echo "Gateway $gw failed to receive configs" >&2
        fi
    done

    echo ""
    if [[ $failed -gt 0 ]]; then
        echo -e "${BOLD}push-config: $failed gateway(s) failed${RST}"
        return 1
    fi
    echo -e "${BOLD}push-config: all gateways up to date${RST}"
}

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
        args+=("$gw_id" "cd $REPO_PATH && echo '=== git fetch ===' && git fetch origin && echo '=== git checkout $GIT_BRANCH ===' && git checkout $GIT_BRANCH && echo '=== reset config.h ===' && git checkout -- src/config.h 2>/dev/null; echo '=== git pull ===' && git pull -X theirs origin $GIT_BRANCH && echo '=== pio pkg update ===' && pio pkg update && echo '=== Upgrade complete ==='")
    done

    if [[ ${#args[@]} -eq 0 ]]; then
        echo "No gateways to upgrade."
        return 1
    fi

    run_parallel "upgrade" "${args[@]}"
}

cmd_upload() {
    # Auto-push per-device configs before flashing unless explicitly skipped.
    if [[ -z "$OPT_SKIP_CONFIG" ]]; then
        if ! cmd_push_config; then
            echo "Upload aborted: push-config failed (re-run with --skip-config to override)." >&2
            return 1
        fi
        echo ""
    fi

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

    local -A pids=()
    local -A scp_logs=()
    local -a gw_order=()

    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        local ssh_dest="${GW_SSH[$gw_id]}"
        local scp_log
        scp_log=$(mktemp)
        scp_logs[$gw_id]="$scp_log"
        gw_order+=("$gw_id")

        log_gw "$gw_id" "Fetching logs..."
        scp_gw "$gw_id" "$ssh_dest:$REPO_PATH/logs/$SESSION/*.log" "$local_dir/" >"$scp_log" 2>&1 &
        pids[$gw_id]=$!
    done

    # Wait for all transfers in launch order; total time = slowest gateway
    for gw_id in "${gw_order[@]}"; do
        if wait "${pids[$gw_id]}" 2>/dev/null; then
            log_gw "$gw_id" "OK"
        else
            log_gw "$gw_id" "No logs found for session $SESSION"
        fi
        rm -f "${scp_logs[$gw_id]}"
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

cmd_sync_time() {
    echo -e "${BOLD}Checking time synchronization...${RST}"
    echo ""

    local local_epoch
    local_epoch=$(date +%s)
    local local_time
    local_time=$(date '+%Y-%m-%d %H:%M:%S')

    echo "Local time: $local_time"
    echo ""

    # Check time on each gateway and attempt sync
    local any_offset=0
    for gw_id in "${ACTIVE_GWS[@]}"; do
        [[ -z "${GW_SSH[$gw_id]+x}" ]] && continue
        local color="${GW_COLORS[$gw_id]:-\033[0m}"

        # Capture local epoch (ms) right before SSH to avoid cumulative drift
        local_epoch=$(date +%s%3N)

        # Get remote epoch (ms)
        local remote_epoch
        remote_epoch=$(ssh_gw "$gw_id" "date +%s%3N" 2>/dev/null) || {
            echo -e "  ${color}${gw_id}${RST}  \033[31mUNREACHABLE\033[0m"
            continue
        }

        local offset_ms=$((remote_epoch - local_epoch))
        local abs_offset_ms=${offset_ms#-}

        if [[ $abs_offset_ms -le 2000 ]]; then
            echo -e "  ${color}${gw_id}${RST}  \033[32mOK\033[0m (offset: ${offset_ms}ms)"
        else
            any_offset=1
            echo -e "  ${color}${gw_id}${RST}  \033[33mOFFSET: ${offset_ms}ms\033[0m — attempting sync..."

            # Try timedatectl (might work without sudo)
            ssh_gw "$gw_id" "timedatectl set-ntp true" 2>/dev/null && {
                echo -e "  ${color}${gw_id}${RST}  Enabled NTP via timedatectl"
                continue
            }

            # Try sudo -n date (non-interactive, works if NOPASSWD configured)
            local target_time
            target_time=$(date '+%Y-%m-%d %H:%M:%S')
            ssh_gw "$gw_id" "sudo -n date -s '$target_time'" 2>/dev/null && {
                echo -e "  ${color}${gw_id}${RST}  \033[32mSynced\033[0m via sudo date"
                continue
            }

            echo -e "  ${color}${gw_id}${RST}  \033[31mCannot sync\033[0m (no sudo). Ask admin to run:"
            echo -e "         sudo timedatectl set-ntp true"
        fi
    done

    if [[ $any_offset -gt 0 ]]; then
        echo ""
        echo "Tip: Ask your admin to enable NTP on gateways with offsets:"
        echo "  sudo timedatectl set-ntp true"
        echo "  sudo timedatectl set-timezone Europe/Madrid  # or your timezone"
    fi
}

cmd_all() {
    echo -e "${BOLD}Full deployment: stop -> upgrade -> push-config -> upload+monitor${RST}"
    echo -e "${BOLD}Session: $SESSION | Env: $ENV${RST}"
    [[ -n "$EXP_YAML" ]] && echo -e "${BOLD}Experiment: $EXP_YAML${RST}"
    echo ""

    echo -e "\n${BOLD}=== Phase 1/4: Stop existing monitors ===${RST}\n"
    cmd_stop_monitor || true

    echo -e "\n${BOLD}=== Phase 2/4: Upgrade ===${RST}\n"
    if ! cmd_upgrade; then
        echo ""
        echo "WARNING: Some gateways failed to upgrade. Continue anyway? (y/N)"
        read -r answer
        [[ "$answer" != "y" && "$answer" != "Y" ]] && exit 1
    fi

    echo -e "\n${BOLD}=== Phase 3/4: Push-config ===${RST}\n"
    if [[ -z "$OPT_SKIP_CONFIG" ]]; then
        if ! cmd_push_config; then
            echo ""
            echo "WARNING: push-config failed. Continue anyway? (y/N)"
            read -r answer
            [[ "$answer" != "y" && "$answer" != "Y" ]] && exit 1
        fi
    else
        echo "Skipped (--skip-config)."
    fi

    echo -e "\n${BOLD}=== Phase 4/4: Upload + Monitor ===${RST}\n"
    OPT_MONITOR="1"
    # cmd_upload would re-run push-config; we already did it, so skip it there.
    local prev_skip="$OPT_SKIP_CONFIG"
    OPT_SKIP_CONFIG="1"
    cmd_upload
    local rc=$?
    OPT_SKIP_CONFIG="$prev_skip"
    return $rc
}

# ── Main ─────────────────────────────────────────────────────────────────────

case "$COMMAND" in
    status)       cmd_status ;;
    upgrade)      cmd_upgrade ;;
    push-config)  cmd_push_config ;;
    upload)       cmd_upload ;;
    monitor)      cmd_monitor ;;
    stop-monitor) cmd_stop_monitor ;;
    logs)         cmd_logs ;;
    clean)        cmd_clean ;;
    sync-time)    cmd_sync_time ;;
    run-remote)   cmd_run_remote ;;
    all)          cmd_all ;;
    *)            echo "Unknown command: $COMMAND"; usage 1 ;;
esac
