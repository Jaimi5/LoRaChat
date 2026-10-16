#!/usr/bin/env bash
# gw-monitor.sh - Runs ON a gateway machine
# Manages PlatformIO serial monitors for local devices.
#
# Usage:
#   bash gw-monitor.sh start SESSION ENV DEVICE_ID:PORT [DEVICE_ID:PORT ...]
#   bash gw-monitor.sh stop
#   bash gw-monitor.sh status
#   bash gw-monitor.sh resume     # restart the session saved by start (cron @reboot)
#   bash gw-monitor.sh rotate     # cut the live logs into daily .gz files (cron 00:05)
#
# Internal, shared with gw-upload.sh:
#   bash gw-monitor.sh launch SESSION DEVICE_ID:PORT
#   bash gw-monitor.sh persist SESSION ENV DEVICE_ID:PORT [...]
#
# Each monitor runs in its own session (setsid) inside a `while true` loop, so a
# USB drop restarts it after 5 s instead of ending it, and `stop` takes the whole
# group down. `persist` saves the session to logs/.current_monitor and installs
# the crontab lines that bring it back after a gateway reboot and rotate the logs
# daily; `stop` deletes the file, so a stopped run never comes back by itself.

set -uo pipefail

ACTION="${1:-help}"; shift || true

REPO="${REPO_PATH:-/home/lora/LoRaChat}"
SELF="$REPO/scripts/testbed/gw-monitor.sh"
STATE_FILE="$REPO/logs/.current_monitor"
CRON_TAG="# lorachat-gw-monitor"

# CPU throttle (set by deploy.sh ssh_gw; empty when run standalone)
PIO_NICE="${PIO_NICE:-}"

# Start one looped monitor for DEVICE_ID:PORT and record its PID.
launch_monitor() {
    local session="$1" device_spec="$2"
    local device_id="${device_spec%%:*}"
    local port="${device_spec#*:}"
    # Short ID: last segment after hyphen (E464, 77A4, etc.)
    local short_id="${device_id##*-}"
    local log_dir="$REPO/logs/$session"
    local logfile="$log_dir/monitor-dev-${session}-${short_id}.log"
    mkdir -p "$log_dir"

    echo "Starting monitor: $device_id ($port) -> $logfile"

    # script provides the pseudo-TTY that pio device monitor requires; the read
    # loop adds [YYYY-MM-DD HH:MM:SS.mmm] to each line. Appending (>>) keeps the
    # file safe to truncate from `rotate`.
    setsid nohup bash -c "
        while true; do
            $PIO_NICE script -qfc 'pio device monitor --port $port --filter esp32_exception_decoder' /dev/null 2>&1 | \
            while IFS= read -r line; do
                echo \"[\$(date \"+%Y-%m-%d %H:%M:%S.%3N\")] \$line\"
            done >> \"$logfile\" 2>&1
            echo \"[\$(date \"+%Y-%m-%d %H:%M:%S.%3N\")] [gw-monitor] monitor exited, restarting in 5 s\" >> \"$logfile\"
            sleep 5
        done
    " </dev/null >/dev/null 2>&1 &

    echo "$!:$device_id:$port" >> "$log_dir/.monitor_pids"
}

# Kill every tracked monitor (any session). Prints how many were running.
kill_monitors() {
    local stopped=0
    for pidfile in "$REPO"/logs/*/.monitor_pids; do
        [[ -f "$pidfile" ]] || continue
        local session
        session=$(basename "$(dirname "$pidfile")")
        while IFS=: read -r pid device port; do
            if kill -0 "$pid" 2>/dev/null; then
                # Kill entire process group (loop + script + pio children)
                kill -- -"$pid" 2>/dev/null || kill "$pid" 2>/dev/null || true
                echo "Stopped: $device (PID $pid, session $session)" >&2
                stopped=$((stopped + 1))
            fi
        done < "$pidfile"
        rm -f "$pidfile"
    done
    # Also kill any orphaned pio/script monitor processes
    pkill -f 'pio device monitor' 2>/dev/null || true
    pkill -f 'script.*pio.*monitor' 2>/dev/null || true
    echo "$stopped"
}

# Replace this script's crontab lines (tagged) with the current ones.
install_cron() {
    command -v crontab >/dev/null 2>&1 || { echo "WARNING: crontab not found, no reboot resume or rotation"; return 0; }
    local env_prefix="REPO_PATH=$REPO"
    local cron_log="$REPO/logs/.gw-monitor-cron.log"
    {
        crontab -l 2>/dev/null | grep -vF "$CRON_TAG"
        echo "@reboot sleep 60 && $env_prefix bash $SELF resume >> $cron_log 2>&1 $CRON_TAG"
        echo "5 0 * * * $env_prefix bash $SELF rotate >> $cron_log 2>&1 $CRON_TAG"
    } | crontab - && echo "Crontab: resume @reboot, rotate 00:05"
}

case "$ACTION" in
    start)
        SESSION="${1:?Missing SESSION name}"; shift
        ENV="${1:?Missing ENV}"; shift

        LOG_DIR="$REPO/logs/$SESSION"
        mkdir -p "$LOG_DIR"

        # Kill ALL existing monitors (any session) to free serial ports
        kill_monitors >/dev/null
        > "$LOG_DIR/.monitor_pids"  # Clear/create pidfile

        for device_spec in "$@"; do
            launch_monitor "$SESSION" "$device_spec"
        done
        bash "$SELF" persist "$SESSION" "$ENV" "$@"

        echo ""
        echo "Monitors started. Logs in: $LOG_DIR"
        echo "Use 'gw-monitor.sh status' to check, 'gw-monitor.sh stop' to kill."
        ;;

    launch)
        launch_monitor "${1:?Missing SESSION}" "${2:?Missing DEVICE_ID:PORT}"
        ;;

    persist)
        SESSION="${1:?Missing SESSION}"; shift
        ENV="${1:?Missing ENV}"; shift
        mkdir -p "$REPO/logs"
        {
            printf 'MON_SESSION=%q\n' "$SESSION"
            printf 'MON_ENV=%q\n' "$ENV"
            printf 'MON_DEVICES=(%s)\n' "$(printf '%q ' "$@")"
            # cron has neither the pio PATH nor the nice wrapper deploy.sh exports
            printf 'MON_PATH=%q\n' "$PATH"
            printf 'MON_PIO_NICE=%q\n' "$PIO_NICE"
        } > "$STATE_FILE.tmp" && mv -f "$STATE_FILE.tmp" "$STATE_FILE"
        echo "Saved monitor session '$SESSION' ($# device(s)) to $STATE_FILE"
        install_cron
        ;;

    resume)
        if [[ ! -f "$STATE_FILE" ]]; then
            echo "[$(date '+%F %T')] resume: no saved monitor session"
            exit 0
        fi
        MON_PATH="" MON_PIO_NICE=""
        # shellcheck source=/dev/null
        source "$STATE_FILE"
        echo "[$(date '+%F %T')] resume: session $MON_SESSION, ${#MON_DEVICES[@]} device(s)"
        export PATH="${MON_PATH:-$PATH}" PIO_NICE="$MON_PIO_NICE" REPO_PATH="$REPO"
        exec bash "$SELF" start "$MON_SESSION" "$MON_ENV" "${MON_DEVICES[@]}"
        ;;

    rotate)
        # Only the saved (running) session rotates; stopped sessions are gzipped by stop.
        if [[ ! -f "$STATE_FILE" ]]; then
            echo "[$(date '+%F %T')] rotate: no saved monitor session"
            exit 0
        fi
        # shellcheck source=/dev/null
        source "$STATE_FILE"
        # Run at 00:05, so the file holds yesterday (plus the first minutes of today).
        DAY="${ROTATE_DAY:-$(date -d yesterday +%Y%m%d)}"
        rotated=0
        for live in "$REPO/logs/$MON_SESSION"/monitor-dev-*.log; do
            [[ -f "$live" ]] || continue
            [[ "$live" =~ -[0-9]{8}\.log$ ]] && continue  # a day file left unzipped
            out="${live%.log}-$DAY.log"
            [[ -e "$out" || -e "$out.gz" ]] && continue   # already rotated for $DAY
            # The monitor appends (O_APPEND), so after the truncate it keeps writing
            # at the new end of file; lines landing between cp and truncate are lost.
            cp -p "$live" "$out" && truncate -s 0 "$live" && gzip -f "$out" && rotated=$((rotated + 1))
        done
        echo "[$(date '+%F %T')] rotate: $rotated file(s) -> *-$DAY.log.gz in $MON_SESSION"
        ;;

    stop)
        rm -f "$STATE_FILE"
        stopped=$(kill_monitors)
        if [[ $stopped -eq 0 ]]; then
            echo "No tracked monitors found (orphaned processes cleaned)."
        else
            echo "Stopped $stopped monitor(s)."
            # Compress log files
            compressed=0
            for logfile in "$REPO"/logs/*/*.log; do
                [[ -f "$logfile" ]] || continue
                gzip -f "$logfile" 2>/dev/null && compressed=$((compressed + 1))
            done
            [[ $compressed -gt 0 ]] && echo "Compressed $compressed log file(s)."
        fi
        ;;

    status)
        found=0
        for pidfile in "$REPO"/logs/*/.monitor_pids; do
            [[ -f "$pidfile" ]] || continue
            session=$(basename "$(dirname "$pidfile")")
            echo "Session: $session"
            while IFS=: read -r pid device port; do
                if kill -0 "$pid" 2>/dev/null; then
                    echo "  RUNNING  $device (PID $pid, $port)"
                else
                    echo "  STOPPED  $device (PID $pid, $port)"
                fi
                found=$((found + 1))
            done < "$pidfile"
        done
        if [[ $found -eq 0 ]]; then
            echo "No monitor sessions found."
        fi
        if [[ -f "$STATE_FILE" ]]; then
            echo "Saved for resume: $(grep '^MON_SESSION=' "$STATE_FILE" | cut -d= -f2-)"
        fi
        ;;

    *)
        echo "Usage: gw-monitor.sh {start|stop|status|resume|rotate}"
        echo ""
        echo "  start SESSION ENV DEVICE_ID:PORT [...]  Start monitors (auto-restart, saved for resume)"
        echo "  stop                                     Stop all monitors, forget the saved session"
        echo "  status                                   Show monitor status"
        echo "  resume                                   Restart the saved session (cron @reboot)"
        echo "  rotate                                   Cut live logs into *-YYYYMMDD.log.gz (cron 00:05)"
        exit 1
        ;;
esac
