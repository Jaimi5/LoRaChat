#!/usr/bin/env python3
"""Collects and checks the images of one build into release/<env>/<version>[-<tag>]/.

Checks before anything is copied:
- the app image ends with the SHA-256 that ESP-IDF appends, and it matches the image;
- the image fits the size gate (90 % of the app slot in partitions.csv);
- the version in esp_app_desc_t matches version.txt (or FW_VERSION);
- the version has no .dirty metadata, unless --allow-dirty is given.

Writes info.json with the versions and hashes that the manifest, the server and the bench use.
With --sign-key it also writes the signed manifest.bin and bundle.bin (sign_manifest.py).
The image identity is the appended SHA-256, the same value esp_partition_get_sha256() returns
on the node.
"""
import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import struct
import sys
from pathlib import Path
from typing import Dict, Optional

ROOT = Path(__file__).resolve().parents[2]
SIZE_GATE = 0.90
IMAGE_MAGIC = 0xE9
APP_DESC_MAGIC = 0xABCD5432
APP_DESC_OFFSET = 24 + 8  # image header + first segment header
IMAGE_FILES = ("firmware.bin", "bootloader.bin", "partitions.bin", "ota_data_initial.bin")
DEBUG_FILES = ("firmware.elf", "firmware.map")


class ReleaseError(Exception):
    pass


def default_build_dir(env: str, project_dir: Path = ROOT) -> Path:
    """Build directory of @p env, following the workspace rule of scripts/pio.sh."""
    if os.environ.get("PLATFORMIO_BUILD_DIR"):
        return Path(os.environ["PLATFORMIO_BUILD_DIR"]) / env
    workspace = os.environ.get("PLATFORMIO_WORKSPACE_DIR")
    if not workspace and str(project_dir).startswith("/mnt/"):
        cache = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
        workspace = str(Path(cache) / "pio-ws" / project_dir.name)
    return Path(workspace or project_dir / ".pio") / "build" / env


def appended_sha256(image: bytes) -> bytes:
    """@return the SHA-256 appended to an ESP image, after checking that it matches."""
    if len(image) < 24 + 32 or image[0] != IMAGE_MAGIC:
        raise ReleaseError("not an ESP image")
    if image[23] != 1:
        raise ReleaseError("image has no appended SHA-256")
    digest = image[-32:]
    if hashlib.sha256(image[:-32]).digest() != digest:
        raise ReleaseError("appended SHA-256 does not match the image")
    return digest


def read_app_desc(image: bytes) -> Dict[str, str]:
    """@return the fields of esp_app_desc_t from an app image."""
    end = APP_DESC_OFFSET + 256
    if len(image) < end:
        raise ReleaseError("image too short for esp_app_desc_t")
    magic, = struct.unpack_from("<I", image, APP_DESC_OFFSET)
    if magic != APP_DESC_MAGIC:
        raise ReleaseError("esp_app_desc_t not found")

    def text(offset: int, size: int) -> str:
        raw = image[APP_DESC_OFFSET + offset:APP_DESC_OFFSET + offset + size]
        return raw.split(b"\0", 1)[0].decode("ascii", errors="replace")

    return {
        "version": text(16, 32),
        "project_name": text(48, 32),
        "time": text(80, 16),
        "date": text(96, 16),
        "idf_ver": text(112, 32),
        "elf_sha256": image[APP_DESC_OFFSET + 144:APP_DESC_OFFSET + 176].hex(),
    }


def version_u32(version: str) -> int:
    """@return MAJOR.MINOR.PATCH packed as 0xMMmmpp00 (build metadata after + is ignored)."""
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)(?:\+.*)?$", version)
    if not match or any(int(part) > 255 for part in match.groups()):
        raise ReleaseError(f"version {version!r} is not MAJOR.MINOR.PATCH with parts 0-255")
    major, minor, patch = (int(part) for part in match.groups())
    return (major << 24) | (minor << 16) | (patch << 8)


def app_slot_size(partitions_csv: Path) -> int:
    """@return the size of the first app partition in @p partitions_csv."""
    with partitions_csv.open() as f:
        rows = (row for row in csv.reader(f) if row and not row[0].lstrip().startswith("#"))
        for row in rows:
            fields = [field.strip() for field in row]
            if len(fields) >= 5 and fields[1] == "app":
                return int(fields[4], 0)
    raise ReleaseError(f"no app partition in {partitions_csv}")


def expected_version(project_dir: Path) -> str:
    return os.environ.get("FW_VERSION") or (project_dir / "version.txt").read_text().strip()


def check_build(build_dir: Path, project_dir: Path, allow_dirty: bool) -> Dict[str, object]:
    """Checks the images in @p build_dir. @return the content of info.json."""
    for name in IMAGE_FILES:
        if not (build_dir / name).exists():
            raise ReleaseError(f"{build_dir / name} is missing")
    image = (build_dir / "firmware.bin").read_bytes()
    image_sha = appended_sha256(image)
    desc = read_app_desc(image)

    core = desc["version"].split("+", 1)[0]
    wanted = expected_version(project_dir)
    if core != wanted:
        raise ReleaseError(f"image version {desc['version']} does not match {wanted}")
    if desc["version"].endswith(".dirty") and not allow_dirty:
        raise ReleaseError(f"image {desc['version']} was built from uncommitted changes")

    slot = app_slot_size(project_dir / "partitions.csv")
    if len(image) > slot * SIZE_GATE:
        raise ReleaseError(f"image is {len(image)} B, over {SIZE_GATE:.0%} of the {slot} B slot")

    bootloader = (build_dir / "bootloader.bin").read_bytes()
    return {
        "version": desc["version"],
        "version_u32": version_u32(desc["version"]),
        "project": desc["project_name"],
        "image_size": len(image),
        "slot_size": slot,
        "slot_use_pct": round(100.0 * len(image) / slot, 2),
        "image_sha256": image_sha.hex(),
        "elf_sha256": desc["elf_sha256"],
        "bootloader_sha256": appended_sha256(bootloader).hex(),
        "partitions_sha256": hashlib.sha256((build_dir / "partitions.bin").read_bytes()).hexdigest(),
        "built": f"{desc['date']} {desc['time']}",
        "idf_ver": desc["idf_ver"],
    }


def collect(env: str, build_dir: Path, out_root: Path, project_dir: Path, tag: Optional[str],
            allow_dirty: bool) -> Path:
    """Checks the build and copies it to out_root/<env>/<version>[-<tag>]. @return that dir."""
    info = check_build(build_dir, project_dir, allow_dirty)
    info["env"] = env
    name = info["version"] + (f"-{tag}" if tag else "")
    out_dir = out_root / env / name
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    for file_name in IMAGE_FILES + DEBUG_FILES:
        source = build_dir / file_name
        if source.exists():
            shutil.copy2(source, out_dir / file_name)
    (out_dir / "info.json").write_text(json.dumps(info, indent=2) + "\n")
    return out_dir


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--env", default="tbeam")
    parser.add_argument("--build-dir", type=Path, help="default: the scripts/pio.sh workspace")
    parser.add_argument("--out", type=Path, default=ROOT / "release")
    parser.add_argument("--tag", help="suffix for test images, e.g. t4")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="accept images built from uncommitted changes")
    parser.add_argument("--sign-key", type=Path, help="private key that signs the manifest")
    parser.add_argument("--key-id", type=lambda v: int(v, 0), help="key id of --sign-key")
    parser.add_argument("--allow-downgrade", action="store_true")
    parser.add_argument("--skip-mesh-check", action="store_true")
    args = parser.parse_args(argv)
    if bool(args.sign_key) != (args.key_id is not None):
        parser.error("--sign-key and --key-id go together")

    build_dir = args.build_dir or default_build_dir(args.env)
    try:
        out_dir = collect(args.env, build_dir, args.out, ROOT, args.tag, args.allow_dirty)
        if args.sign_key:
            import sign_manifest

            flags = ((sign_manifest.FLAG_ALLOW_DOWNGRADE if args.allow_downgrade else 0)
                     | (sign_manifest.FLAG_SKIP_MESH_CHECK if args.skip_mesh_check else 0))
            try:
                sign_manifest.sign_release(out_dir, sign_manifest.load_key(args.sign_key),
                                           args.key_id, flags)
            except sign_manifest.ManifestError as exc:
                raise ReleaseError(str(exc)) from exc
    except (OSError, ReleaseError, ValueError) as exc:
        print(f"release: {exc}", file=sys.stderr)
        return 1
    info = json.loads((out_dir / "info.json").read_text())
    print(f"{out_dir}: {info['version']}, {info['image_size']} B ({info['slot_use_pct']} %), "
          f"sha256 {info['image_sha256'][:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
