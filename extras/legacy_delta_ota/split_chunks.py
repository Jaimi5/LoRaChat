#!/usr/bin/env python3
"""Split a firmware or patch binary into numbered chunks for OTA transport."""

import base64
import json
import os

import click
import crcmod

MQTT_CHUNK_SIZE = 400
LORA_CHUNK_SIZE = 222


def crc16_ccitt(data: bytes) -> int:
    crc16_func = crcmod.mkCrcFun(0x11021, initCrc=0xFFFF, xorOut=0x0000)
    return crc16_func(data)


@click.command()
@click.option("--input", "input_file", required=True, type=click.Path(exists=True), help="Input binary file")
@click.option("--transport", required=True, type=click.Choice(["mqtt", "lora"]), help="Transport type")
@click.option("--output-dir", required=True, type=click.Path(), help="Output directory for chunks")
def main(input_file: str, transport: str, output_dir: str):
    """Split INPUT binary into chunks for MQTT or LoRa transport."""
    chunk_size = MQTT_CHUNK_SIZE if transport == "mqtt" else LORA_CHUNK_SIZE

    os.makedirs(output_dir, exist_ok=True)

    with open(input_file, "rb") as f:
        data = f.read()

    total_chunks = (len(data) + chunk_size - 1) // chunk_size
    click.echo(f"Input: {input_file} ({len(data)} bytes)")
    click.echo(f"Transport: {transport}, chunk size: {chunk_size} bytes")

    for seq_num in range(total_chunks):
        offset = seq_num * chunk_size
        chunk_data = data[offset : offset + chunk_size]
        crc16 = crc16_ccitt(chunk_data)

        if transport == "mqtt":
            chunk_info = {
                "seq_num": seq_num,
                "chunk_size": len(chunk_data),
                "crc16": crc16,
                "data_b64": base64.b64encode(chunk_data).decode("ascii"),
            }
            out_path = os.path.join(output_dir, f"chunk_{seq_num:06d}.json")
            with open(out_path, "w") as f:
                json.dump(chunk_info, f, indent=2)
        else:
            out_path = os.path.join(output_dir, f"chunk_{seq_num:06d}.bin")
            with open(out_path, "wb") as f:
                f.write(chunk_data)

    click.echo(f"Total chunks: {total_chunks}")
    click.echo(f"Output directory: {output_dir}")


if __name__ == "__main__":
    main()
