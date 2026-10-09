#!/usr/bin/env python3
"""Flashes the floor image on one node over USB and provisions it for the field.

    python flash_floor.py release\\tbeam\\0.2.0+gabc1234 --port COM6 --key-file deploy.key ^
        [--wifi-ssid FarmNet] [--role sensor|gateway] [--keep-nvs] [--inventory nodes.csv]

Steps: full flash (bootloader, partition table, NVS erased unless --keep-nvs, otadata erased so
app0 boots, the image in app0), one reset, wait for the BOOT line, then over the serial console
/key.set (the fingerprint must match the key file), optionally /maint.wifi (the password is asked
for and never shown) and /role.set. A row with MAC, node address, version, bootloader hash, key
fingerprint, AP name and WiFi QR text is appended to the inventory CSV.

The inventory holds the AP passwords (in the QR text): keep it like the deployment key.
Run with Windows Python (pyserial, esptool from PlatformIO); WSL loses USB devices.
"""
import argparse
import csv
import getpass
import os
import re
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ap_pass  # noqa: E402

NVS_OFFSET = "0x9000"
NVS_SIZE = 0x5000
BAUD = 115200
FLASH_BAUD = "921600"
INVENTORY_FIELDS = ["time", "mac", "node", "ver", "bl", "key", "ap_ssid", "wifi_qr", "role"]


class FloorError(Exception):
    pass


@dataclass
class ReleaseImages:
    bootloader: Path
    partitions: Path
    otadata: Path
    firmware: Path
    blank_nvs: Path

    @classmethod
    def from_folder(cls, folder: Path) -> "ReleaseImages":
        folder = Path(folder)
        return cls(folder / "bootloader.bin", folder / "partitions.bin",
                   folder / "ota_data_initial.bin", folder / "firmware.bin",
                   Path(tempfile.gettempdir()) / f"lm_blank_nvs_{NVS_SIZE:#x}.bin")

    def check(self) -> None:
        for path in (self.bootloader, self.partitions, self.otadata, self.firmware):
            if not path.is_file():
                raise FloorError(f"{path} not found")
        if not self.blank_nvs.is_file():
            self.blank_nvs.write_bytes(b"\xff" * NVS_SIZE)


def esptool_args(port: str, images: ReleaseImages, erase_nvs: bool) -> list:
    flash = ["0x1000", str(images.bootloader), "0x8000", str(images.partitions)]
    if erase_nvs:
        flash += [NVS_OFFSET, str(images.blank_nvs)]
    flash += ["0xe000", str(images.otadata), "0x10000", str(images.firmware)]
    return ["--chip", "esp32", "--port", port, "--baud", FLASH_BAUD, "--after", "no_reset",
            "write_flash"] + flash


def parse_mac(esptool_output: str) -> str:
    match = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", esptool_output)
    if not match:
        raise FloorError("esptool did not print the MAC address")
    return match.group(1).lower()


def parse_boot(line: str) -> Dict[str, str]:
    return dict(re.findall(r"(\w+)=(\S+)", line[line.index("BOOT"):]))


def parse_node(version_reply: str) -> int:
    match = re.search(r"node=([0-9A-Fa-f]{4})", version_reply)
    if not match:
        raise FloorError(f"no node address in {version_reply!r}")
    return int(match.group(1), 16)


def read_line(port, deadline: float) -> Optional[str]:
    while time.monotonic() < deadline:
        line = port.readline().decode(errors="replace").strip()
        if line:
            return line
    return None


def wait_for_boot(port, timeout: float = 30.0) -> Dict[str, str]:
    port.reset()
    deadline = time.monotonic() + timeout
    while True:
        line = read_line(port, deadline)
        if line is None:
            raise FloorError("the node did not print its BOOT line")
        if "BOOT part=" in line:
            return parse_boot(line)


def command(port, line: str, timeout: float = 10.0) -> str:
    name = line.split(" ")[0]
    port.write((line + "\n").encode())
    deadline = time.monotonic() + timeout
    while True:
        text = read_line(port, deadline)
        if text is None:
            raise FloorError(f"no reply to {name}")
        if text == f"> {name}":
            reply = read_line(port, deadline)
            if reply is None:
                raise FloorError(f"no reply to {name}")
            return reply


def provision(port, key: bytes, wifi: Optional[Tuple[str, str]] = None,
              role: Optional[str] = None, timeout: float = 10.0) -> Dict[str, str]:
    version = command(port, "/version", timeout)
    node = parse_node(version)
    fingerprint = ap_pass.key_fingerprint(key)
    reply = command(port, f"/key.set {key.hex()}", timeout)
    if fingerprint not in reply:
        raise FloorError(f"key fingerprint mismatch: node answered {reply!r}")
    if wifi is not None:
        reply = command(port, f"/maint.wifi {wifi[0]} {wifi[1]}", timeout)
        if "stored" not in reply:
            raise FloorError(f"/maint.wifi failed: {reply}")
    if role is not None:
        reply = command(port, f"/role.set {role}", timeout)
        if "Role set" not in reply:
            raise FloorError(f"/role.set failed: {reply}")
    fields = dict(re.findall(r"(\w+)=(\S+)", version))
    return {"time": time.strftime("%Y-%m-%d %H:%M:%S"), "node": f"{node:04X}",
            "ver": version.split(" ")[0], "bl": fields.get("bl", ""), "key": fingerprint,
            "ap_ssid": ap_pass.ap_ssid(node), "wifi_qr": ap_pass.wifi_qr(key, node),
            "role": role or ""}


def append_inventory(path: Path, row: Dict[str, str]) -> None:
    new = not path.exists()
    with path.open("a", newline="") as out:
        writer = csv.DictWriter(out, fieldnames=INVENTORY_FIELDS, extrasaction="ignore")
        if new:
            writer.writeheader()
        writer.writerow(row)


class SerialPort:
    """pyserial port opened without toggling DTR/RTS; reset() pulses RTS once."""

    def __init__(self, name: str):
        import serial
        self.port = serial.Serial()
        self.port.port = name
        self.port.baudrate = BAUD
        self.port.timeout = 0.2
        self.port.dtr = False
        self.port.rts = False
        self.port.open()

    def reset(self) -> None:
        self.port.reset_input_buffer()
        self.port.rts = True
        time.sleep(0.1)
        self.port.rts = False

    def readline(self) -> bytes:
        return self.port.readline()

    def write(self, data: bytes) -> int:
        return self.port.write(data)

    def close(self) -> None:
        self.port.close()


def default_esptool() -> Path:
    home = Path(os.environ.get("USERPROFILE") or Path.home())
    return home / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("release", type=Path, help="release folder written by release.py")
    parser.add_argument("--port", required=True)
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--wifi-ssid", help="store this network (the password is asked for)")
    parser.add_argument("--role", choices=["sensor", "gateway"])
    parser.add_argument("--keep-nvs", action="store_true", help="do not erase NVS")
    parser.add_argument("--inventory", type=Path, default=Path("nodes.csv"))
    parser.add_argument("--esptool", type=Path, default=default_esptool())
    args = parser.parse_args(argv)

    try:
        key = ap_pass.load_key(args.key_file)
        wifi = None
        if args.wifi_ssid:
            wifi = (args.wifi_ssid, getpass.getpass(f"Password for {args.wifi_ssid}: "))
        images = ReleaseImages.from_folder(args.release)
        images.check()

        flashed = subprocess.run([sys.executable, str(args.esptool)] +
                                 esptool_args(args.port, images, not args.keep_nvs),
                                 capture_output=True, text=True)
        if flashed.returncode != 0:
            raise FloorError(f"esptool failed:\n{flashed.stdout[-2000:]}{flashed.stderr[-2000:]}")
        mac = parse_mac(flashed.stdout)
        print(f"Flashed {args.release.name} on {args.port} (MAC {mac})")

        port = SerialPort(args.port)
        try:
            boot = wait_for_boot(port)
            print(f"BOOT part={boot.get('part')} state={boot.get('state')} ver={boot.get('ver')}")
            # Give the node time to start its services and the serial console.
            time.sleep(3)
            row = provision(port, key, wifi, args.role)
        finally:
            port.close()
        row["mac"] = mac
        append_inventory(args.inventory, row)
        print(f"Node {row['node']} ready: {row['ap_ssid']}, key {row['key']}, "
              f"bootloader {row['bl']}; inventory {args.inventory}")
    except (FloorError, ap_pass.KeyFileError, OSError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
