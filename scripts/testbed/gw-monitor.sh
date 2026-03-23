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

REPO="${REPO_PATH:-/home/lora/LoRaChat}"

case "$ACTION" in
    start)
        SESSION="${1:?Missing SESSION name}"; shift
        ENV="${1:?Missing ENV}"; shift

        LOG_DIR="$REPO/logs/$SESSION"
        mkdir -p "$LOG_DIR"
        PIDFILE="$LOG_DIR/.monitor_pids"

        # Kill ALL existing monitors (any session) to free serial ports
        for old_pidfile in "$REPO"/logs/*/.monitor_pids; do
            [[ -f "$old_pidfile" ]] || continue
            while IFS=: read -r pid device port; do
                if kill -0 "$pid" 2>/dev/null; then
                    kill "$pid" 2>/dev/null || true
                    echo "Stopped previous monitor: $device (PID $pid)"
                fi
            done < "$old_pidfile"
            rm -f "$old_pidfile"
        done
        # Also kill any orphaned pio/script monitor processes
        pkill -f 'pio device monitor' 2>/dev/null || true
        pkill -f 'script.*pio.*monitor' 2>/dev/null || true
        > "$PIDFILE"  # Clear/create pidfile

        for device_spec in "$@"; do
            DEVICE_ID="${device_spec%%:*}"
            PORT="${device_spec#*:}"
            # Extract short ID: last segment after hyphen (E464, 77A4, etc.)
            SHORT_ID="${DEVICE_ID##*-}"

            LOGFILE="$LOG_DIR/monitor-dev-${SESSION}-${SHORT_ID}.log"

            echo "Starting monitor: $DEVICE_ID ($PORT) -> $LOGFILE"

            # Use script to provide a pseudo-TTY (pio device monitor requires one)
            # Pipe through a timestamp loop to add [YYYY-MM-DD HH:MM:SS] to each line
            nohup bash -c "
                script -qfc 'pio device monitor --port $PORT --filter esp32_exception_decoder' /dev/null 2>&1 | \
                while IFS= read -r line; do
                    echo \"[\$(date \"+%Y-%m-%d %H:%M:%S.%3N\")] \$line\"
                done >> \"$LOGFILE\" 2>&1
            " </dev/null >/dev/null 2>&1 &

            echo "$!:$DEVICE_ID:$PORT" >> "$PIDFILE"
        done

        echo ""
        echo "Monitors started. Logs in: $LOG_DIR"
        echo "Use 'gw-monitor.sh status' to check, 'gw-monitor.sh stop' to kill."
        ;;

    stop)
        stopped=0
        # Kill tracked PIDs from pidfiles
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
        # Also kill any orphaned pio/script monitor processes
        pkill -f 'pio device monitor' 2>/dev/null && echo "Killed orphaned pio monitor processes." || true
        pkill -f 'script.*pio.*monitor' 2>/dev/null || true
        if [[ $stopped -eq 0 ]]; then
            echo "No tracked monitors found (orphaned processes cleaned)."
        else
            echo "Stopped $stopped monitor(s)."
            # Compress log files
            local compressed=0
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
