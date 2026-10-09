#!/usr/bin/env python3
"""Bench: uploads a bundle to a node's maintenance access point through a bridge board.

The bridge is a second T-Beam running extras/bench_ap_bridge, connected over USB. It joins the
node's access point and forwards what this script streams to it, so the bench PC never leaves
its own network. Open the window on the node first (/maint.open 900 ap), then:

    python ap_upload.py COM3 --key-file deploy.key --node 7680 --status
    python ap_upload.py COM3 --key-file deploy.key --node 7680 --bundle bundle.bin
    python ap_upload.py COM3 --key-file deploy.key --node 7680 --bundle bundle.bin --cut-at 0.5

The access point password is derived from the deployment key (ap_pass.py) and never printed.
Needs pyserial (PlatformIO's Windows Python has it).
"""
import argparse
import sys
import time
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ap_pass  # noqa: E402

BLOCK_SIZE = 4096
BAUD = 921600


class BridgeError(Exception):
    pass


class Bridge:
    """Line protocol of extras/bench_ap_bridge over a serial port."""

    def __init__(self, port, timeout: float = 90.0):
        self.port = port
        self.timeout = timeout

    def _send(self, line: str) -> None:
        self.port.write((line + "\n").encode())

    def _expect(self, *prefixes: str, timeout: Optional[float] = None) -> str:
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            line = self.port.readline().decode(errors="replace").strip()
            if not line:
                continue
            if line.startswith("ERR"):
                raise BridgeError(line)
            if line.startswith(prefixes):
                return line
        raise BridgeError(f"no {' or '.join(prefixes)} from the bridge")

    def _response(self) -> str:
        lines = []
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            line = self.port.readline().decode(errors="replace").rstrip("\r\n")
            if line == "END":
                return "\n".join(lines).strip()
            lines.append(line)
        raise BridgeError("response not finished")

    def sync(self, attempts: int = 10, wait: float = 1.0) -> None:
        """Waits until the bridge answers, e.g. while it boots after a reset."""
        for _ in range(attempts):
            self._send("PING")
            try:
                self._expect("PONG", timeout=min(wait, self.timeout))
                return
            except BridgeError:
                continue
        raise BridgeError("the bridge does not answer")

    def join(self, ssid: str, password: str) -> str:
        self._send(f"JOIN {ssid} {password}")
        return self._expect("OK JOINED")

    def get(self, path: str) -> str:
        self._send(f"GET {path}")
        self._expect("RESPONSE")
        return self._response()

    def post(self, path: str, data: bytes, send: Optional[int] = None, progress=None) -> str:
        send = len(data) if send is None else send
        self._send(f"POST {path} {len(data)} {send}")
        self._expect("READY")
        sent = 0
        while sent < send:
            block = data[sent:sent + min(BLOCK_SIZE, send - sent)]
            self.port.write(block)
            sent += len(block)
            self._expect("ACK")
            if progress:
                progress(sent)
        if send < len(data):
            return self._expect("CUT")
        self._expect("RESPONSE")
        return self._response()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("port", help="serial port of the bridge, e.g. COM3")
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--node", required=True, help="mesh address of the node in hex")
    parser.add_argument("--status", action="store_true", help="print GET /status")
    parser.add_argument("--bundle", type=Path, help="bundle.bin to upload")
    parser.add_argument("--cut-at", type=float, help="close the upload after this fraction")
    args = parser.parse_args(argv)

    import serial  # only needed with a real bridge

    key = ap_pass.load_key(args.key_file)
    node = ap_pass.parse_node(args.node)
    port = serial.Serial()
    port.port = args.port
    port.baudrate = BAUD
    port.timeout = 1
    port.dtr = False
    port.rts = False
    port.open()
    bridge = Bridge(port)
    try:
        bridge.sync()
        print(f"{ap_pass.ap_ssid(node)}: {bridge.join(ap_pass.ap_ssid(node), ap_pass.ap_password(key, node))}")
        if args.status:
            print(bridge.get("/status"))
        if args.bundle:
            data = args.bundle.read_bytes()
            send = int(len(data) * args.cut_at) if args.cut_at is not None else len(data)
            start = time.monotonic()
            reply = bridge.post("/update", data, send)
            print(f"Sent {send} of {len(data)} B in {time.monotonic() - start:.1f} s")
            print(reply)
    except BridgeError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    finally:
        port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
