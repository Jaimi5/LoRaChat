#!/usr/bin/env bash
# gw-reset.sh - Runs ON a gateway machine
# Hard-resets ESP32 devices by pulsing DTR/RTS via pyserial. Equivalent to
# esptool.py's `--after hard_reset` but doesn't require esptool to be on PATH
# (pyserial is bundled with PlatformIO; esptool.py lives under
# ~/.platformio/packages/tool-esptoolpy/ with a versioned filename).
#
# Usage:
#   bash gw-reset.sh DEVICE_ID:PORT [DEVICE_ID:PORT ...]
#
# Exits 0 iff every reset succeeded.

set -uo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: gw-reset.sh DEVICE_ID:PORT [DEVICE_ID:PORT ...]" >&2
    exit 2
fi

python3 - "$@" <<'PYEOF'
import sys
import time

try:
    import serial
except ImportError as e:
    print(f"ERROR: pyserial not available: {e}", file=sys.stderr)
    sys.exit(3)

ok = fail = 0
for spec in sys.argv[1:]:
    dev_id, _, port = spec.partition(":")
    if not port:
        print(f"  [reset] {spec}: FAIL (expected DEVICE_ID:PORT)")
        fail += 1
        continue
    try:
        with serial.Serial(port, 115200) as s:
            # ESP32 USB-UART wiring: DTR drives IO0, RTS drives EN (often
            # via a transistor pair). Holding DTR=False keeps IO0 high so
            # we hard-reset into the application, not the bootloader.
            s.dtr = False
            s.rts = True   # EN low — reset asserted
            time.sleep(0.1)
            s.rts = False  # EN high — released, chip boots normally
            time.sleep(0.05)
        print(f"  [reset] {dev_id} ({port}): OK")
        ok += 1
    except Exception as e:
        print(f"  [reset] {dev_id} ({port}): FAIL ({type(e).__name__}: {e})")
        fail += 1

print(f"Reset summary: {ok} OK, {fail} failed")
sys.exit(0 if fail == 0 else 1)
PYEOF
