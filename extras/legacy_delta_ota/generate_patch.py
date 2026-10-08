#!/usr/bin/env python3
"""Generate a delta patch between two firmware binaries and optionally split into OTA chunks."""

import base64
import json
import math
import os
import tempfile

import click
import crcmod
import detools

MQTT_CHUNK_SIZE = 400
LORA_CHUNK_SIZE = 222


def compute_crc32(filepath: str) -> int:
    crc32_func = crcmod.predefined.mkCrcFun("crc-32")
    with open(filepath, "rb") as f:
        return crc32_func(f.read())


def crc16_ccitt(data: bytes) -> int:
    crc16_func = crcmod.mkCrcFun(0x11021, initCrc=0xFFFF, xorOut=0x0000, rev=False)
    return crc16_func(data)


def split_into_chunks(patch_path: str, transport: str, output_dir: str) -> int:
    """Split patch binary into numbered chunk files. Returns total chunk count."""
    chunk_size = MQTT_CHUNK_SIZE if transport == "mqtt" else LORA_CHUNK_SIZE
    os.makedirs(output_dir, exist_ok=True)

    with open(patch_path, "rb") as f:
        data = f.read()

    total_chunks = math.ceil(len(data) / chunk_size)

    for seq_num in range(total_chunks):
        chunk_data = data[seq_num * chunk_size : (seq_num + 1) * chunk_size]
        crc16 = crc16_ccitt(chunk_data)

        if transport == "mqtt":
            out_path = os.path.join(output_dir, f"chunk_{seq_num:06d}.json")
            with open(out_path, "w") as f:
                json.dump({
                    "seq_num": seq_num,
                    "chunk_size": len(chunk_data),
                    "crc16": crc16,
                    "data_b64": base64.b64encode(chunk_data).decode("ascii"),
                }, f, indent=2)
        else:
            out_path = os.path.join(output_dir, f"chunk_{seq_num:06d}.bin")
            with open(out_path, "wb") as f:
                f.write(chunk_data)

    return total_chunks


@click.command()
@click.option("--old", "old_fw", required=True, type=click.Path(exists=True),
              help="Path to old firmware binary")
@click.option("--new", "new_fw", required=True, type=click.Path(exists=True),
              help="Path to new firmware binary")
@click.option("--out", "out_patch", default=None, type=click.Path(),
              help="Output patch file path (default: <new_fw>.patch.bin)")
@click.option("--version", "fw_version", default="unknown",
              help="Firmware version string (embedded in metadata)")
@click.option("--split", "do_split", is_flag=True, default=False,
              help="Also split the patch into OTA chunks after generation")
@click.option("--transport", type=click.Choice(["mqtt", "lora"]), default="lora",
              show_default=True, help="Chunk transport type (used with --split)")
@click.option("--output-dir", "output_dir", default=None, type=click.Path(),
              help="Output directory for chunks (used with --split; default: <out_patch>_chunks/)")
def main(old_fw, new_fw, out_patch, fw_version, do_split, transport, output_dir):
    """Generate a delta patch from OLD and NEW firmware, and optionally split into OTA chunks.

    Always produces <out>.bin + <out>.json (metadata).
    The .json sidecar is required by ota_campaign.py --delta.

    With --split: also splits the patch into numbered chunk files ready for inspection.
    The patch binary is always kept — ota_campaign.py reads it directly.

    Examples:

    \b
    # Patch only (typical workflow — then run ota_campaign.py --delta)
    python generate_patch.py --old v1.bin --new v2.bin --out patch.bin --version 2.0.0

    \b
    # Patch + split for LoRa in one step
    python generate_patch.py --old v1.bin --new v2.bin --version 2.0.0 --split --transport lora

    \b
    # Patch + split for MQTT
    python generate_patch.py --old v1.bin --new v2.bin --split --transport mqtt
    """
    # Resolve defaults
    if out_patch is None:
        out_patch = os.path.splitext(new_fw)[0] + ".patch.bin"
    if output_dir is None:
        output_dir = os.path.splitext(out_patch)[0] + "_chunks"

    old_size = os.path.getsize(old_fw)
    new_size = os.path.getsize(new_fw)

    click.echo(f"Old firmware : {old_fw} ({old_size:,} bytes)")
    click.echo(f"New firmware : {new_fw} ({new_size:,} bytes)")
    click.echo(f"Generating patch → {out_patch} ...")

    with open(old_fw, "rb") as old_f, open(new_fw, "rb") as new_f, open(out_patch, "wb") as patch_f:
        detools.create_patch(old_f, new_f, patch_f, compression="heatshrink")

    patch_size = os.path.getsize(out_patch)
    new_crc32 = compute_crc32(new_fw)

    metadata = {
        "old_size": old_size,
        "new_size": new_size,
        "patch_size": patch_size,
        "new_crc32": new_crc32,
        "fw_version": fw_version,
    }
    if do_split:
        chunk_size = MQTT_CHUNK_SIZE if transport == "mqtt" else LORA_CHUNK_SIZE
        metadata["transport"] = transport
        metadata["chunk_size"] = chunk_size
        metadata["total_chunks"] = math.ceil(patch_size / chunk_size)

    json_path = os.path.splitext(out_patch)[0] + ".json"
    with open(json_path, "w") as f:
        json.dump(metadata, f, indent=2)

    click.echo(f"Patch size   : {patch_size:,} bytes ({patch_size / new_size:.1%} of new firmware)")
    click.echo(f"New CRC32    : 0x{new_crc32:08X}")
    click.echo(f"Version      : {fw_version}")
    click.echo(f"Metadata     : {json_path}")

    if do_split:
        click.echo(f"\nSplitting into {transport.upper()} chunks → {output_dir}/ ...")
        total_chunks = split_into_chunks(out_patch, transport, output_dir)
        chunk_size = MQTT_CHUNK_SIZE if transport == "mqtt" else LORA_CHUNK_SIZE
        click.echo(f"Chunks       : {total_chunks} × {chunk_size} B  ({output_dir}/)")

    click.echo(f"Patch file   : {out_patch}")

    click.echo("\nDone.")


if __name__ == "__main__":
    main()
