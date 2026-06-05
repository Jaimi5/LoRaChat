#!/usr/bin/env bash
# gw-upgrade.sh - Runs ON a gateway machine
# Updates the repo and PlatformIO dependencies.
#
# Usage: bash gw-upgrade.sh [BRANCH]

set -euo pipefail

BRANCH="${1:-new_loramesher}"
REPO="${REPO_PATH:-/home/lora/LoRaChat}"

cd "$REPO" || { echo "ERROR: $REPO not found"; exit 1; }

echo "=== git fetch ==="
git fetch origin

echo "=== git checkout $BRANCH ==="
git checkout "$BRANCH"

echo "=== reset config.h ==="
git checkout -- src/config.h 2>/dev/null || true

echo "=== git pull ==="
git pull -X theirs origin "$BRANCH"

echo "=== pio pkg update ==="
${PIO_NICE:-} pio pkg update

echo "=== Upgrade complete ==="
