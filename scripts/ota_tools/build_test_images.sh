#!/usr/bin/env bash
# Builds the boot-guard test images and collects them with release.py.
#
# Each image is the normal tbeam firmware with one TEST_IMAGE_KIND fault and the next patch
# version, so it is newer than the running image:
#   t1 crash, t2 irq-hang, t3 loop-hang, t4 selftest-fail (see src/ota/otaBootGuard.h)
# Images are signed with the test key (key id 1), which production builds refuse.
# Output: release/tbeam/<version>-t<kind>/. Usage: scripts/ota_tools/build_test_images.sh [kinds]
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$project_dir"

kinds="${*:-1 2 3 4}"
next_version="$(awk -F. '{ printf "%d.%d.%d", $1, $2, $3 + 1 }' version.txt)"

for kind in $kinds; do
    echo "== TEST_IMAGE_KIND=$kind, version $next_version"
    FW_VERSION="$next_version" PLATFORMIO_BUILD_FLAGS="-D TEST_IMAGE_KIND=$kind" \
        scripts/pio.sh run -e tbeam
    FW_VERSION="$next_version" \
        python3 scripts/ota_tools/release.py --env tbeam --tag "t$kind" --allow-dirty \
        --sign-key test/vectors/test_key.pem --key-id 1
done
