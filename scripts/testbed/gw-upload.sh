#!/usr/bin/env bash
# gw-upload.sh - Runs ON a gateway machine
# Sequentially configures, compiles, and uploads firmware for each device.
# Optionally starts a monitor immediately after each successful upload.
#
# Usage: bash gw-upload.sh ENV [--skip-compile] [--monitor SESSION] DEVICE_ID:PORT [...]
#
# ENV               — PlatformIO environment (e.g., ttgo-t-beam-v2)
# --skip-compile    — skip compilation, only upload (firmware must already be built)
# --monitor SESSION — start a monitor for each device right after uploading
# DEVICE_ID:PORT    — device ID and serial port pairs

set -uo pipefail  # Not -e: continue on per-device failures

ENV="$1"; shift

SKIP_COMPILE=0
MONITOR_SESSION=""

# Parse optional flags
while [[ "${1:-}" == --* ]]; do
    case "$1" in
        --skip-compile) SKIP_COMPILE=1; shift ;;
        --monitor) MONITOR_SESSION="$2"; shift 2 ;;
        *) break ;;
    esac
done

REPO="${REPO_PATH:-/home/lora/LoRaChat}"
cd "$REPO" || { echo "ERROR: $REPO not found"; exit 1; }

# If monitoring, stop any existing monitors first
if [[ -n "$MONITOR_SESSION" ]]; then
    echo "--- Stopping existing monitors ---"
    bash "$REPO/scripts/testbed/gw-monitor.sh" stop || true
    LOG_DIR="$REPO/logs/$MONITOR_SESSION"
    mkdir -p "$LOG_DIR"
    PIDFILE="$LOG_DIR/.monitor_pids"
    > "$PIDFILE"
fi

TOTAL=0
SUCCEEDED=0
FAILED_DEVICES=""

for device_spec in "$@"; do
    DEVICE_ID="${device_spec%%:*}"
    PORT="${device_spec#*:}"
    TOTAL=$((TOTAL + 1))

    echo ""
    echo "========================================"
    echo "  Device $TOTAL/$#: $DEVICE_ID"
    echo "  Port: $PORT"
    echo "  Env:  $ENV"
    echo "========================================"

    # Step 1: Run change-config script (uses SHORT_ID, e.g., 7B6C)
    SHORT_ID="${DEVICE_ID##*-}"
    CONFIG_SCRIPT="change-config-${SHORT_ID}.sh"
    if [[ -x "$REPO/$CONFIG_SCRIPT" ]]; then
        echo "--- Running ./$CONFIG_SCRIPT ---"
        (cd "$REPO" && ./"$CONFIG_SCRIPT")
    elif [[ -f "$REPO/$CONFIG_SCRIPT" ]]; then
        echo "--- Running $CONFIG_SCRIPT (via bash) ---"
        (cd "$REPO" && bash "$CONFIG_SCRIPT")
    elif [[ -x "$REPO/scripts/testbed/$CONFIG_SCRIPT" ]]; then
        echo "--- Running scripts/testbed/$CONFIG_SCRIPT ---"
        (cd "$REPO" && bash "scripts/testbed/$CONFIG_SCRIPT")
    else
        echo "WARNING: $CONFIG_SCRIPT not found in repo root or scripts/testbed/"
        echo "         Proceeding with current src/config.h"
    fi

    # Step 2: Compile + Upload
    if [[ $SKIP_COMPILE -eq 1 ]]; then
        echo "--- Upload only (--skip-compile) ---"
        if pio run -e "$ENV" --target upload --upload-port "$PORT"; then
            echo "OK: $DEVICE_ID uploaded on $PORT"
            SUCCEEDED=$((SUCCEEDED + 1))
        else
            echo "FAILED: $DEVICE_ID upload on $PORT"
            FAILED_DEVICES="$FAILED_DEVICES $DEVICE_ID"
        fi
    else
        echo "--- Compile + Upload ---"
        if pio run -e "$ENV" --target upload --upload-port "$PORT"; then
            echo "OK: $DEVICE_ID compiled and uploaded on $PORT"
            SUCCEEDED=$((SUCCEEDED + 1))
        else
            echo "FAILED: $DEVICE_ID on $PORT"
            FAILED_DEVICES="$FAILED_DEVICES $DEVICE_ID"
        fi
    fi

    # Step 3: Start monitor immediately after successful upload
    if [[ -n "$MONITOR_SESSION" && -z "$(echo "$FAILED_DEVICES" | grep "$DEVICE_ID")" ]]; then
        LOGFILE="$LOG_DIR/monitor-dev-${MONITOR_SESSION}-${SHORT_ID}.log"
        echo "--- Starting monitor: $PORT -> $LOGFILE ---"
        nohup bash -c "
            script -qfc 'pio device monitor --port $PORT --filter esp32_exception_decoder' /dev/null 2>&1 | \
            while IFS= read -r line; do
                echo \"[\$(date \"+%Y-%m-%d %H:%M:%S.%3N\")] \$line\"
            done >> \"$LOGFILE\" 2>&1
        " </dev/null >/dev/null 2>&1 &
        echo "$!:$DEVICE_ID:$PORT" >> "$PIDFILE"
        echo "Monitor started for $DEVICE_ID"
    fi
done

echo ""
echo "========================================"
echo "  Upload summary: $SUCCEEDED/$TOTAL succeeded"
if [[ -n "$FAILED_DEVICES" ]]; then
    echo "  Failed:$FAILED_DEVICES"
fi
if [[ -n "$MONITOR_SESSION" ]]; then
    echo "  Monitors running: $SUCCEEDED device(s)"
    echo "  Logs: $LOG_DIR/"
fi
echo "========================================"

[[ $SUCCEEDED -eq $TOTAL ]]
