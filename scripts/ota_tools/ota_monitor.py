#!/usr/bin/env python3
"""Monitor OTA progress by subscribing to device MQTT status messages."""

import json
import signal
import sys

import click
import paho.mqtt.client as mqtt
from tqdm import tqdm

OTA_MSG_TYPES = {1: "BEGIN", 2: "CHUNK", 3: "ACK", 4: "APPLY", 5: "STATUS", 6: "ABORT"}

# Per-node progress bars keyed by node address
_progress_bars: dict[str, tqdm] = {}


def _get_progress_bar(node_addr: str, total: int = 0) -> tqdm:
    if node_addr not in _progress_bars:
        _progress_bars[node_addr] = tqdm(
            total=total, desc=f"Node {node_addr}", unit="chunk", dynamic_ncols=True
        )
    bar = _progress_bars[node_addr]
    if total and bar.total != total:
        bar.total = total
        bar.refresh()
    return bar


def _on_connect(client, userdata, flags, rc):
    if rc == 0:
        click.echo("Connected to MQTT broker")
        client.subscribe("to-server/+")
    else:
        click.echo(f"Connection failed with code {rc}")


def _on_message(client, userdata, msg):
    topic = msg.topic
    parts = topic.split("/")
    node_addr = parts[1] if len(parts) >= 2 else "unknown"

    try:
        payload = json.loads(msg.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        return

    data = payload.get("data", {})
    msg_type = data.get("type")
    type_name = OTA_MSG_TYPES.get(msg_type, f"UNKNOWN({msg_type})")

    if msg_type == 3:  # ACK
        status = data.get("status", -1)
        seq = data.get("seq_num", -1)
        bar = _get_progress_bar(node_addr)
        if status == 0:
            bar.update(1)
        else:
            status_names = {0: "ok", 1: "crc_error", 2: "out_of_order", 3: "no_space"}
            reason = status_names.get(status, f"error({status})")
            tqdm.write(f"[{node_addr}] ACK seq={seq} status={reason}")

    elif msg_type == 5:  # STATUS
        chunks_rx = data.get("chunks_received", 0)
        total = data.get("total_chunks", 0)
        state = data.get("state", -1)
        bar = _get_progress_bar(node_addr, total=total)
        bar.n = chunks_rx
        bar.refresh()
        tqdm.write(f"[{node_addr}] STATUS state={state} progress={chunks_rx}/{total}")

    elif msg_type == 6:  # ABORT
        reason = data.get("reason", -1)
        tqdm.write(f"[{node_addr}] ABORT reason={reason}")
        if node_addr in _progress_bars:
            _progress_bars[node_addr].close()
            del _progress_bars[node_addr]

    else:
        tqdm.write(f"[{node_addr}] {type_name}: {payload}")


@click.command()
@click.option("--host", default="192.168.1.26", help="MQTT broker host")
@click.option("--port", default=1883, type=int, help="MQTT broker port")
@click.option("--username", default="admin", help="MQTT username")
@click.option("--password", default="public", help="MQTT password")
def main(host: str, port: int, username: str, password: str):
    """Monitor OTA progress from all nodes via MQTT."""
    client = mqtt.Client()
    client.username_pw_set(username, password)
    client.on_connect = _on_connect
    client.on_message = _on_message

    def _signal_handler(sig, frame):
        click.echo("\nShutting down...")
        for bar in _progress_bars.values():
            bar.close()
        client.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGINT, _signal_handler)

    click.echo(f"Connecting to {host}:{port}...")
    try:
        client.connect(host, port, keepalive=60)
    except OSError as e:
        click.echo(f"Connection error: {e}")
        sys.exit(1)

    client.loop_forever()


if __name__ == "__main__":
    main()
