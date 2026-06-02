#!/usr/bin/env python3
"""OTA firmware deployment orchestrator — streams chunks to a target node via MQTT."""

import base64
import json
import os
import random
import signal
import sys
import threading
import time

import click
import crcmod
import paho.mqtt.client as mqtt
from tqdm import tqdm

OTA_CHUNK_TIMEOUT_MS = 10000
OTA_MAX_RETRIES = 5
MQTT_CHUNK_SIZE = 400
LORA_CHUNK_SIZE = 222
OTA_APP_PORT = 18  # appPort::OTAApp — must match dataMessage.h


def crc16_ccitt(data: bytes) -> int:
    crc16_func = crcmod.mkCrcFun(0x11021, initCrc=0xFFFF, xorOut=0x0000, rev=False)
    return crc16_func(data)


def crc32(data: bytes) -> int:
    crc32_func = crcmod.predefined.mkCrcFun("crc-32")
    return crc32_func(data)


class OTACampaign:
    def __init__(self, client: mqtt.Client, target: str, session_id: int,
                 chunks: list[bytes], total_size: int, fw_crc32: int,
                 is_delta: bool, fw_version: str):
        self.client = client
        self.target = target
        self.session_id = session_id
        self.chunks = chunks
        self.total_size = total_size
        self.fw_crc32 = fw_crc32
        self.is_delta = is_delta
        self.fw_version = fw_version

        self.ack_event = threading.Event()
        self.last_ack_seq = -1
        self.last_ack_status = -1
        self.aborted = False
        self.abort_reason = ""

    def _wrap(self, seq_num: int, ota_payload: dict) -> dict:
        """Wrap an OTA payload in the DataMessage envelope the manager expects."""
        return {
            "data": {
                "appPortSrc": OTA_APP_PORT,
                "appPortDst": OTA_APP_PORT,
                "addrSrc": 0,
                "addrDst": 0,  # filled from topic by mqttService
                "messageId": seq_num,
                "messageSize": 0,
                **ota_payload,
            }
        }

    def _publish(self, topic: str, payload: dict):
        self.client.publish(topic, json.dumps(payload))

    def _on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return

        data = payload.get("data", {})
        msg_type = data.get("type")

        if msg_type == 3:  # ACK
            if data.get("session_id") == self.session_id:
                self.last_ack_seq = data.get("seq_num", -1)
                self.last_ack_status = data.get("status", -1)
                self.ack_event.set()

        elif msg_type == 6:  # ABORT
            if data.get("session_id") == self.session_id:
                self.aborted = True
                self.abort_reason = f"reason={data.get('reason', 'unknown')}"
                self.ack_event.set()

    def run(self) -> bool:
        topic_sub = f"to-server/{self.target}"
        topic_pub = f"from-server/{self.target}"

        self.client.on_message = self._on_message
        self.client.subscribe(topic_sub)

        # Send OTA_BEGIN
        begin_msg = {
            "type": 1,  # OTA_BEGIN
            "session_id": self.session_id,
            "total_size": self.total_size,
            "total_chunks": len(self.chunks),
            "fw_crc32": self.fw_crc32,
            "is_delta": 1 if self.is_delta else 0,
            "fw_version": self.fw_version,
        }
        click.echo(f"Sending OTA_BEGIN to {self.target} (session 0x{self.session_id:08X})")
        self._publish(topic_pub, self._wrap(0, begin_msg))

        # Wait for BEGIN ACK
        self.ack_event.clear()
        if not self.ack_event.wait(timeout=OTA_CHUNK_TIMEOUT_MS / 1000):
            click.echo("ERROR: No ACK for OTA_BEGIN — aborting")
            return False
        if self.aborted:
            click.echo(f"ERROR: Node aborted session ({self.abort_reason})")
            return False

        # Stream chunks
        bar = tqdm(total=len(self.chunks), desc="OTA chunks", unit="chunk")
        for seq_num, chunk_data in enumerate(self.chunks):
            crc16 = crc16_ccitt(chunk_data)
            chunk_msg = {
                "type": 2,  # OTA_CHUNK
                "session_id": self.session_id,
                "seq_num": seq_num,
                "chunk_size": len(chunk_data),
                "crc16": crc16,
                "data_b64": base64.b64encode(chunk_data).decode("ascii"),
            }

            for attempt in range(OTA_MAX_RETRIES):
                self.ack_event.clear()
                self._publish(topic_pub, self._wrap(seq_num, chunk_msg))

                if self.ack_event.wait(timeout=OTA_CHUNK_TIMEOUT_MS / 1000):
                    if self.aborted:
                        bar.close()
                        click.echo(f"ERROR: Node aborted session ({self.abort_reason})")
                        return False
                    if self.last_ack_status == 0 and self.last_ack_seq == seq_num:
                        bar.update(1)
                        break
                    else:
                        tqdm.write(f"  Chunk {seq_num}: bad ACK (status={self.last_ack_status}), retry {attempt + 1}")
                else:
                    tqdm.write(f"  Chunk {seq_num}: timeout, retry {attempt + 1}")
            else:
                bar.close()
                click.echo(f"ERROR: Chunk {seq_num} failed after {OTA_MAX_RETRIES} retries")
                self._publish(topic_pub, self._wrap(seq_num, {
                    "type": 6,  # OTA_ABORT
                    "session_id": self.session_id,
                    "reason": 1,
                }))
                return False

        bar.close()

        # Send OTA_APPLY
        apply_msg = {
            "type": 4,  # OTA_APPLY
            "session_id": self.session_id,
        }
        click.echo("Sending OTA_APPLY...")
        self._publish(topic_pub, self._wrap(len(self.chunks), apply_msg))

        # Wait for final ACK
        self.ack_event.clear()
        if self.ack_event.wait(timeout=OTA_CHUNK_TIMEOUT_MS / 1000 * 3):
            if self.last_ack_status == 0:
                click.echo("OTA completed successfully!")
                return True
            else:
                click.echo(f"ERROR: OTA_APPLY ACK status={self.last_ack_status}")
                return False
        else:
            click.echo("WARNING: No ACK for OTA_APPLY (node may be rebooting)")
            return True


@click.command()
@click.option("--target", required=True, help="Target node address (decimal 15071 or hex 0x3ADF)")
@click.option("--firmware", required=True, type=click.Path(exists=True), help="Firmware or patch binary")
@click.option("--delta", is_flag=True, default=False, help="Treat firmware file as a delta patch")
@click.option("--transport", default="mqtt", type=click.Choice(["mqtt", "lora"]), help="Transport type")
@click.option("--host", default="192.168.1.26", help="MQTT broker host")
@click.option("--port", default=1883, type=int, help="MQTT broker port")
@click.option("--username", default="admin", help="MQTT username")
@click.option("--password", default="public", help="MQTT password")
@click.option("--version", "fw_version", default="unknown", help="Firmware version string")
def main(target: str, firmware: str, delta: bool, transport: str,
         host: str, port: int, username: str, password: str, fw_version: str):
    """Deploy firmware OTA to a target node via MQTT."""
    # Normalise target to decimal string — the node publishes to "to-server/15071" not "to-server/0x3ADF"
    target = str(int(target, 0) if target.startswith("0x") or target.startswith("0X") else int(target))

    chunk_size = MQTT_CHUNK_SIZE if transport == "mqtt" else LORA_CHUNK_SIZE

    with open(firmware, "rb") as f:
        fw_data = f.read()

    session_id = random.randint(0, 0xFFFFFFFF)

    # For delta OTA the verification target is the new firmware, not the patch.
    # Read new_size and new_crc32 from the sidecar JSON produced by generate_patch.py.
    if delta:
        json_path = os.path.splitext(firmware)[0] + ".json"
        if not os.path.exists(json_path):
            click.echo(f"ERROR: Delta metadata not found: {json_path}")
            click.echo("Generate it with generate_patch.py --old old.bin --new new.bin --out patch.bin")
            sys.exit(1)
        with open(json_path) as f:
            meta = json.load(f)
        total_size = meta["new_size"]
        fw_crc = meta["new_crc32"]
        click.echo(f"Delta metadata: new_size={total_size}, new_crc32=0x{fw_crc:08X}")
    else:
        total_size = len(fw_data)
        fw_crc = crc32(fw_data)

    # Split into chunks
    chunks = []
    for offset in range(0, len(fw_data), chunk_size):
        chunks.append(fw_data[offset : offset + chunk_size])

    if delta:
        click.echo(f"Firmware: {firmware} ({len(fw_data)} bytes patch → {total_size} bytes new, CRC32=0x{fw_crc:08X})")
    else:
        click.echo(f"Firmware: {firmware} ({total_size} bytes, CRC32=0x{fw_crc:08X})")
    click.echo(f"Target: {target}, transport: {transport}, chunks: {len(chunks)}")
    click.echo(f"Delta: {delta}, version: {fw_version}")

    client = mqtt.Client()
    client.username_pw_set(username, password)

    def _signal_handler(sig, frame):
        click.echo("\nAborting...")
        client.disconnect()
        sys.exit(1)

    signal.signal(signal.SIGINT, _signal_handler)

    try:
        client.connect(host, port, keepalive=60)
    except OSError as e:
        click.echo(f"Connection error: {e}")
        sys.exit(1)

    client.loop_start()

    campaign = OTACampaign(
        client=client,
        target=target,
        session_id=session_id,
        chunks=chunks,
        total_size=total_size,
        fw_crc32=fw_crc,
        is_delta=delta,
        fw_version=fw_version,
    )

    success = campaign.run()

    client.loop_stop()
    client.disconnect()

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
