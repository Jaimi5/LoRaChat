#!/usr/bin/env bash
# gw-reset.sh - Runs ON a gateway machine
# Hard-resets ESP32 devices by pulsing DTR/RTS via pyserial, then captures
# the first few seconds of post-reset serial output and appends it to the
# per-device monitor log. This is what gives us proof in the logs that the
# device actually rebooted between runs — the boot banner (rst:0x, app_main,
# our "Build environment name:" log) would otherwise be lost in the gap
# between reset and `gw-monitor.sh start`.
#
# Usage:
#   bash gw-reset.sh SESSION DEVICE_ID:PORT [DEVICE_ID:PORT ...]
#
# Env:
#   REPO_PATH         repo root on the gateway (default /home/lora/LoRaChat)
#   RESET_CAPTURE_MS  how long to read the port post-reset (default 3000)
#
# Exits 0 iff every reset succeeded.

set -uo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: gw-reset.sh SESSION DEVICE_ID:PORT [DEVICE_ID:PORT ...]" >&2
    exit 2
fi

SESSION="$1"; shift
REPO="${REPO_PATH:-/home/lora/LoRaChat}"
CAPTURE_MS="${RESET_CAPTURE_MS:-3000}"
LOG_DIR="$REPO/logs/$SESSION"
mkdir -p "$LOG_DIR"

export _GW_RESET_LOG_DIR="$LOG_DIR"
export _GW_RESET_CAPTURE_MS="$CAPTURE_MS"

python3 - "$@" <<'PYEOF'
import os
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError as e:
    print(f"ERROR: pyserial not available: {e}", file=sys.stderr)
    sys.exit(3)

LOG_DIR = os.environ["_GW_RESET_LOG_DIR"]
SESSION = os.path.basename(LOG_DIR)
CAPTURE_S = max(0.0, int(os.environ.get("_GW_RESET_CAPTURE_MS", "3000")) / 1000.0)


def _ts() -> str:
    now = datetime.now()
    return now.strftime("[%Y-%m-%d %H:%M:%S.") + f"{now.microsecond // 1000:03d}]"


def _capture(port_obj, log_path: str, deadline: float) -> int:
    """Read from `port_obj` until `deadline`, appending timestamped lines to
    `log_path`. Returns the number of lines written. The port stays open the
    whole time — that's how we keep the kernel from releasing it back to a
    racing monitor start before we're done."""
    written = 0
    # Short read timeout so we can check the deadline frequently. We assemble
    # lines ourselves because pyserial's readline() blocks per-line.
    port_obj.timeout = 0.1
    buf = bytearray()
    with open(log_path, "ab") as fh:
        while time.monotonic() < deadline:
            chunk = port_obj.read(4096)
            if chunk:
                buf.extend(chunk)
                while True:
                    nl = buf.find(b"\n")
                    if nl < 0:
                        break
                    line = bytes(buf[: nl + 1])
                    del buf[: nl + 1]
                    text = line.decode("utf-8", errors="replace").rstrip("\r\n")
                    fh.write(f"{_ts()} {text}\n".encode("utf-8"))
                    written += 1
        # Flush any trailing partial line so we don't lose the first chars of
        # the banner if it didn't end with a newline within the window.
        if buf:
            text = bytes(buf).decode("utf-8", errors="replace").rstrip("\r\n")
            if text:
                fh.write(f"{_ts()} {text}\n".encode("utf-8"))
                written += 1
    return written


ok = fail = 0
for spec in sys.argv[1:]:
    dev_id, _, port = spec.partition(":")
    if not port:
        print(f"  [reset] {spec}: FAIL (expected DEVICE_ID:PORT)")
        fail += 1
        continue

    short_id = dev_id.rsplit("-", 1)[-1]
    log_path = os.path.join(LOG_DIR, f"monitor-dev-{SESSION}-{short_id}.log")

    try:
        with serial.Serial(port, 115200) as s:
            # ESP32 USB-UART wiring: DTR drives IO0, RTS drives EN (often
            # via a transistor pair). Holding DTR=False keeps IO0 high so
            # we hard-reset into the application, not the bootloader.
            s.dtr = False
            s.rts = True   # EN low — reset asserted
            time.sleep(0.1)
            s.rts = False  # EN high — released, chip boots normally

            # Mark the reset moment in the log so the boundary is unambiguous
            # when reading the file later.
            with open(log_path, "ab") as fh:
                fh.write(f"{_ts()} --- RESET via gw-reset.sh (capturing {int(CAPTURE_S*1000)}ms) ---\n".encode("utf-8"))

            lines = _capture(s, log_path, time.monotonic() + CAPTURE_S)
        print(f"  [reset] {dev_id} ({port}): OK  ({lines} boot lines captured)")
        ok += 1
    except Exception as e:
        print(f"  [reset] {dev_id} ({port}): FAIL ({type(e).__name__}: {e})")
        fail += 1

print(f"Reset summary: {ok} OK, {fail} failed")
sys.exit(0 if fail == 0 else 1)
PYEOF
