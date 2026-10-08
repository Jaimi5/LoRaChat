#!/usr/bin/env python3
"""PlatformIO post-build script: auto-generate delta patch when OLD_FW is set.

Usage in platformio.ini:
    extra_scripts = post:scripts/ota_tools/build_and_deploy.py
"""

import os
import subprocess
import sys


def after_build(source, target, env):
    """Post-action hook called after a successful firmware build."""
    old_fw = os.environ.get("OLD_FW")
    firmware_path = str(target[0])

    if not old_fw:
        print("=" * 60)
        print("OTA delta patch generation skipped.")
        print("To auto-generate a patch, set the OLD_FW environment variable:")
        print(f"  OLD_FW=/path/to/old_firmware.bin pio run -e <env>")
        print("=" * 60)
        return

    if not os.path.isfile(old_fw):
        print(f"ERROR: OLD_FW file not found: {old_fw}")
        return

    script_dir = os.path.dirname(os.path.abspath(__file__))
    gen_script = os.path.join(script_dir, "generate_patch.py")

    build_dir = os.path.dirname(firmware_path)
    patch_path = os.path.join(build_dir, "patch.bin")

    fw_version = os.environ.get("FW_VERSION", "unknown")

    print(f"Generating delta patch: {old_fw} -> {firmware_path}")
    try:
        subprocess.run(
            [
                sys.executable, gen_script,
                "--old", old_fw,
                "--new", firmware_path,
                "--out", patch_path,
                "--version", fw_version,
            ],
            check=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"ERROR: Patch generation failed (exit code {e.returncode})")
    except FileNotFoundError:
        print(f"ERROR: generate_patch.py not found at {gen_script}")


# PlatformIO integration: hook into the build
try:
    Import("env")  # type: ignore[name-defined]
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)  # type: ignore[name-defined]
except Exception:
    # Running standalone (not from PlatformIO)
    if __name__ == "__main__":
        print("This script is designed to run as a PlatformIO extra_script.")
        print("Add to platformio.ini:")
        print("  extra_scripts = post:scripts/ota_tools/build_and_deploy.py")
