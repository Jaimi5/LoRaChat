#!/usr/bin/env bash
# fix-gw4-mtu.sh — Make the MTU fix on GW-4 permanent.
#
# Why: GW-4 (enp1s0) sits on a network segment whose path-MTU is below 1500.
# Full-size 1500-byte frames get black-holed (the "fragmentation needed" ICMP
# is filtered), so the TLS handshake to github.com stalls and resets — which
# surfaces as `git fetch/pull: Recv failure: Connection reset by peer` during
# `deploy.sh upgrade`. Measured path-MTU ceiling is 1450; we pin 1400 for margin.
#
# This script runs FROM your laptop and SSHes into GW-4. It is idempotent:
# re-running it will not duplicate config lines.
#
# Usage:
#   bash scripts/testbed/fix-gw4-mtu.sh                 # uses defaults below
#   HOST=lora@10.1.27.17 IFACE=enp1s0 MTU=1400 bash scripts/testbed/fix-gw4-mtu.sh
#   # GW-7 (same path-MTU issue, static ifupdown config):
#   HOST=lora@10.139.40.6 IFACE=enp2s0 METHOD=static bash scripts/testbed/fix-gw4-mtu.sh
#
# Requirements: sudo on the GW-4 host (you will be prompted for its password
# once, after the SSH password).

set -euo pipefail

HOST="${HOST:-lora@10.1.27.17}"
IFACE="${IFACE:-enp1s0}"
MTU="${MTU:-1400}"
METHOD="${METHOD:-dhcp}"   # inet method of the stanza in /etc/network/interfaces
REPO="${REPO:-/home/lora/LoRaChat}"

echo "==> Making MTU=$MTU permanent on $HOST ($IFACE)"
echo

# The remote logic. References $IFACE/$MTU/$REPO, which we prepend below.
# IMPORTANT: this is delivered to the gateway as a base64 ARGUMENT, not on
# ssh's stdin. Putting a heredoc on ssh's stdin stops `ssh -t` from allocating
# a remote terminal, which then breaks `sudo`'s password prompt. base64 keeps
# stdin free for the tty and sidesteps all quoting pitfalls.
REMOTE_BODY=$(cat <<'REMOTE'
set -euo pipefail

FILE=/etc/network/interfaces
STANZA="iface ${IFACE} inet ${METHOD}"

echo "--- before ---"
grep -nA3 "^${STANZA}" "$FILE" || { echo "ERROR: stanza '$STANZA' not found in $FILE"; exit 1; }
echo "live MTU now: $(cat /sys/class/net/${IFACE}/mtu)"
echo

# Backup once (don't clobber an existing backup).
sudo cp -n "$FILE" "${FILE}.bak"

# Add an `mtu` stanza (applied at boot by ifupdown) if not already present.
if grep -qE "^[[:space:]]*mtu ${MTU}\b" "$FILE"; then
    echo "mtu ${MTU} already present — skipping"
else
    sudo sed -i "/^${STANZA}/a\\    mtu ${MTU}" "$FILE"
    echo "added: mtu ${MTU}"
fi

# Add a post-up hook (re-asserts MTU after DHCP brings the link up) if absent.
if grep -qE "post-up .*set dev ${IFACE} mtu ${MTU}\b" "$FILE"; then
    echo "post-up hook already present — skipping"
else
    sudo sed -i "/^${STANZA}/a\\    post-up /sbin/ip link set dev ${IFACE} mtu ${MTU}" "$FILE"
    echo "added: post-up hook"
fi

# Apply immediately — no reboot needed.
sudo ip link set dev "${IFACE}" mtu "${MTU}"

echo
echo "--- after ---"
grep -nA4 "^${STANZA}" "$FILE"
echo "live MTU now: $(cat /sys/class/net/${IFACE}/mtu)"

echo
echo "--- verify ---"
if ping -M do -c1 -W2 -s $((MTU - 28)) 140.82.121.3 >/dev/null 2>&1; then
    echo "frame size OK (${MTU}-byte frames pass)"
else
    echo "WARN: ${MTU}-byte frames still blocked — lower MTU further"
fi
if git -C "${REPO}" ls-remote origin -h >/dev/null 2>&1; then
    echo "github reachable ✓"
else
    echo "STILL FAILING — github not reachable"
fi
REMOTE
)

# Prepend the variable assignments, then base64-encode the whole thing.
FULL="IFACE='${IFACE}'
MTU='${MTU}'
METHOD='${METHOD}'
REPO='${REPO}'
${REMOTE_BODY}"

B64=$(printf '%s' "$FULL" | base64 | tr -d '\n')

# -t forces a remote pty so sudo can prompt for its password. stdin stays the
# local terminal because the script travels as an argument, not via stdin.
ssh -t "$HOST" "echo ${B64} | base64 -d | bash"

echo
echo "Done. Expected above: 'frame size OK' and 'github reachable ✓'."
