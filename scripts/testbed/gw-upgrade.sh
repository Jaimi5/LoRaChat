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

# Gateways never author commits: mirror the remote exactly instead of pulling,
# so a force-pushed branch doesn't abort with "divergent branches".
# reset --hard only touches tracked files; untracked per-device configs stay.
echo "=== sync to origin/$BRANCH (old HEAD -> backup/pre-sync) ==="
git branch -f backup/pre-sync HEAD
git checkout -B "$BRANCH" "origin/$BRANCH"
git reset --hard "origin/$BRANCH"

echo "=== pio pkg update ==="
${PIO_NICE:-} pio pkg update

echo "=== Upgrade complete ==="
