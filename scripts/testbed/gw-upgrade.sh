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

echo "=== git pull ==="
git pull origin "$BRANCH"

echo "=== pio pkg update ==="
pio pkg update

echo "=== Upgrade complete ==="
