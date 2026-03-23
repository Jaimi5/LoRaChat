#!/usr/bin/env bash
# gw-monitor.sh - Runs ON a gateway machine
# Manages PlatformIO serial monitors for local devices.
#
# Usage:
#   bash gw-monitor.sh start SESSION ENV DEVICE_ID:PORT [DEVICE_ID:PORT ...]
#   bash gw-monitor.sh stop
#   bash gw-monitor.sh status

set -uo pipefail

ACTION="${1:-help}"; shift || true

REPO="${REPO_PATH:-$HOME/LoRaChat}"

case "$ACTION" in
    start)
        SESSION="${1:?Missing SESSION name}"; shift
        ENV="${1:?Missing ENV}"; shift

        LOG_DIR="$REPO/logs/$SESSION"
        mkdir -p "$LOG_DIR"
        PIDFILE="$LOG_DIR/.monitor_pids"

        # Kill any existing monitors from a previous session with the same name
        if [[ -f "$PIDFILE" ]]; then
            echo "Stopping previous monitors for session: $SESSION"
            while IFS=: read -r pid device port; do
                if kill -0 "$pid" 2>/dev/null; then
                    kill "$pid" 2>/dev/null || true
                fi
            done < "$PIDFILE"
        fi
        > "$PIDFILE"  # Clear/create pidfile

        for device_spec in "$@"; do
            DEVICE_ID="${device_spec%%:*}"
            PORT="${device_spec#*:}"
            # Extract short ID: last segment after hyphen (E464, 77A4, etc.)
            SHORT_ID="${DEVICE_ID##*-}"

            LOGFILE="$LOG_DIR/monitor-dev-${SESSION}-${SHORT_ID}.log"

            echo "Starting monitor: $DEVICE_ID ($PORT) -> $LOGFILE"

            # Use nohup so monitor survives SSH disconnect
            nohup pio device monitor \
                --environment "$ENV" \
                --port "$PORT" \
                --filter time \
                --filter esp32_exception_decoder \
                >> "$LOGFILE" 2>&1 &

            echo "$!:$DEVICE_ID:$PORT" >> "$PIDFILE"
        done

        echo ""
        echo "Monitors started. Logs in: $LOG_DIR"
        echo "Use 'gw-monitor.sh status' to check, 'gw-monitor.sh stop' to kill."
        ;;

    stop)
        stopped=0
        for pidfile in "$REPO"/logs/*/.monitor_pids; do
            [[ -f "$pidfile" ]] || continue
            session=$(basename "$(dirname "$pidfile")")
            while IFS=: read -r pid device port; do
                if kill -0 "$pid" 2>/dev/null; then
                    kill "$pid" 2>/dev/null && echo "Stopped: $device (PID $pid, session $session)" || true
                    stopped=$((stopped + 1))
                fi
            done < "$pidfile"
            rm -f "$pidfile"
        done
        if [[ $stopped -eq 0 ]]; then
            echo "No running monitors found."
        else
            echo "Stopped $stopped monitor(s)."
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
        ;;

    *)
        echo "Usage: gw-monitor.sh {start|stop|status}"
        echo ""
        echo "  start SESSION ENV DEVICE_ID:PORT [...]  Start monitors"
        echo "  stop                                     Stop all monitors"
        echo "  status                                   Show monitor status"
        exit 1
        ;;
esac
