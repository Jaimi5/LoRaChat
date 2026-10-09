#!/usr/bin/env python3
"""Bench firmware server: serves a release folder to the nodes over plain HTTP.

The nodes download manifest.bin and firmware.bin from OTA_SERVER_URL. Point it at this server
(http://<pc>:<port>/) in config_local.h and run:

    python fw_server.py release/tbeam/0.1.1+gabc1234

Faults for the bench cases apply to firmware.bin only:

    --flip-byte N          one byte flipped (case 3: SHA mismatch, nothing installed)
    --cut-at F             close the connection after a fraction F of the image (case 4)
    --cut-times K          only cut the first K downloads (default: all)
    --missing-image        firmware.bin answers 404

Standard library only, so it runs with any Python 3 on Windows.
"""
import argparse
import sys
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional, Tuple

DEFAULT_PORT = 8070
IMAGE_FILE = "firmware.bin"


@dataclass
class Faults:
    flip_byte: Optional[int] = None
    cut_at: Optional[float] = None
    cut_times: Optional[int] = None
    missing_image: bool = False


def body_for(name: str, data: bytes, faults: Faults, request: int) -> Tuple[bytes, int]:
    """Returns the bytes to send and the Content-Length to announce for the n-th image request."""
    if name != IMAGE_FILE:
        return data, len(data)
    body = bytearray(data)
    if faults.flip_byte is not None:
        body[faults.flip_byte] ^= 0x01
    if faults.cut_at is not None and (faults.cut_times is None or request <= faults.cut_times):
        return bytes(body[:int(len(body) * faults.cut_at)]), len(data)
    return bytes(body), len(data)


def make_server(directory, port: int, faults: Faults, bind: str = "0.0.0.0"):
    directory = Path(directory)
    state = {"image_requests": 0}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            self.respond(send_body=True)

        def do_HEAD(self):  # noqa: N802
            self.respond(send_body=False)

        def respond(self, send_body: bool):
            name = self.path.lstrip("/")
            path = directory / name
            if ("/" in name or "\\" in name or not path.is_file()
                    or (name == IMAGE_FILE and faults.missing_image)):
                self.send_error(404)
                return
            request = 0
            if name == IMAGE_FILE and send_body:
                state["image_requests"] += 1
                request = state["image_requests"]
            body, length = body_for(name, path.read_bytes(), faults, request)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(length))
            self.end_headers()
            if not send_body:
                return
            start = time.monotonic()
            self.wfile.write(body)
            self.wfile.flush()
            self.log_message("%s: sent %d of %d B in %.1f s", name, len(body), length,
                             time.monotonic() - start)
            if len(body) < length:
                self.close_connection = True

    return ThreadingHTTPServer((bind, port), Handler)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("directory", help="release folder with manifest.bin and firmware.bin")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--flip-byte", type=int)
    parser.add_argument("--cut-at", type=float)
    parser.add_argument("--cut-times", type=int)
    parser.add_argument("--missing-image", action="store_true")
    args = parser.parse_args(argv)

    directory = Path(args.directory)
    for name in ("manifest.bin", IMAGE_FILE):
        if not (directory / name).is_file():
            print(f"Error: {directory / name} not found", file=sys.stderr)
            return 1
    faults = Faults(args.flip_byte, args.cut_at, args.cut_times, args.missing_image)
    server = make_server(directory, args.port, faults)
    print(f"Serving {directory} on port {args.port} ({faults})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
