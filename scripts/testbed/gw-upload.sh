#!/usr/bin/env bash
# gw-upload.sh - Runs ON a gateway machine
# Sequentially configures, compiles, and uploads firmware for each device.
#
# Usage: bash gw-upload.sh ENV [--skip-compile] DEVICE_ID:PORT [DEVICE_ID:PORT ...]
#
# ENV             — PlatformIO environment (e.g., ttgo-t-beam-v2)
# --skip-compile  — skip compilation, only upload (firmware must already be built)
# DEVICE_ID:PORT  — device ID and serial port pairs

set -uo pipefail  # Not -e: continue on per-device failures

ENV="$1"; shift

SKIP_COMPILE=0
if [[ "${1:-}" == "--skip-compile" ]]; then
    SKIP_COMPILE=1
    shift
fi

REPO="${REPO_PATH:-$HOME/LoRaChat}"
cd "$REPO" || { echo "ERROR: $REPO not found"; exit 1; }

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

    # Step 1: Run change-config script
    CONFIG_SCRIPT="change-config-${DEVICE_ID}.sh"
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
done

echo ""
echo "========================================"
echo "  Upload summary: $SUCCEEDED/$TOTAL succeeded"
if [[ -n "$FAILED_DEVICES" ]]; then
    echo "  Failed:$FAILED_DEVICES"
fi
echo "========================================"

[[ $SUCCEEDED -eq $TOTAL ]]
