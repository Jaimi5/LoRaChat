#!/usr/bin/env bash
# Runs PlatformIO for this project. Under WSL, when the project lives on a Windows drive
# (/mnt/<drive>), the PlatformIO workspace (build, libdeps, build_cache) is placed on the
# Linux filesystem, because /mnt/<drive> is served over 9P and makes PlatformIO's CMake
# reading and library dependency scan several times slower.
#
# Usage: scripts/pio.sh run -e ttgo-t-beam-v2-wifi
#        scripts/pio.sh test -e native
# Set PLATFORMIO_WORKSPACE_DIR to override the workspace location.
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pio_bin="${PIO_BIN:-$HOME/.platformio/penv/bin/pio}"

if [[ -z "${PLATFORMIO_WORKSPACE_DIR:-}" && "$project_dir" == /mnt/* ]]; then
    export PLATFORMIO_WORKSPACE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/pio-ws/$(basename "$project_dir")"
fi
echo "PlatformIO workspace: ${PLATFORMIO_WORKSPACE_DIR:-$project_dir/.pio}" >&2

cd "$project_dir"
exec "$pio_bin" "$@"
