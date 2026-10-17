#!/usr/bin/env python3
"""Flashes the floor image on nodes over USB, provisions them for the field and checks them.

    python flash_floor.py release\\tbeam\\0.2.0+gabc1234 --port COM6[,COM7,...] ^
        --key-file deploy.key [--wifi-ssid FarmNet] [--role sensor|gateway] [--keep-nvs] ^
        [--inventory nodes.csv] [--check [--ota-server URL]]

Steps per port (several ports run in parallel): full flash (bootloader, partition table, NVS
erased unless --keep-nvs, otadata erased so app0 boots, the image in app0), one reset, wait for
the BOOT line, then over the serial console /key.set (the fingerprint must match the key file),
optionally /maint.wifi (the password is asked for once and never shown) and /role.set. A row
with MAC, node address, version, bootloader hash, key fingerprint, AP name and WiFi QR text is
appended to the inventory CSV.

--check then tests each node before it joins the deployment: it boots VALID with the bootloader
of the release, a power chip answers, the key is stored, the radio hears the mesh (a gateway must
be running) and a pull window reaches the OTA server and gets a signed report accepted. The
result goes into the inventory row, a table is printed, and nodes that pass blink their LEDs.

The inventory holds the AP passwords (in the QR text): keep it like the deployment key.
Run with Windows Python (pyserial, esptool from PlatformIO); WSL loses USB devices.
"""
import argparse
import csv
import getpass
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ap_pass  # noqa: E402

NVS_OFFSET = "0x9000"
NVS_SIZE = 0x5000
BAUD = 115200
FLASH_BAUD = "921600"
INVENTORY_FIELDS = ["time", "mac", "node", "ver", "bl", "key", "ap_ssid", "wifi_qr", "role",
                    "pmu", "checks", "result"]
CHECKS = ["boot", "bl", "pmu", "key", "mesh", "wifi"]
MESH_POLL_S = 10.0
CHECK_WINDOW_S = 90
BLINK_S = 120


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

    def expected_bootloader(self) -> str:
        """@return the bootloader hash prefix the node prints (bl=...), from info.json."""
        info = self.firmware.parent / "info.json"
        if not info.is_file():
            return ""
        return json.loads(info.read_text()).get("bootloader_sha256", "")[:6]


def esptool_args(port: str, images: ReleaseImages, erase_nvs: bool) -> list:
    flash = ["0x1000", str(images.bootloader), "0x8000", str(images.partitions)]
    if erase_nvs:
        flash += [NVS_OFFSET, str(images.blank_nvs)]
    flash += ["0xe000", str(images.otadata), "0x10000", str(images.firmware)]
    return ["--chip", "esp32", "--port", port, "--baud", FLASH_BAUD, "--after", "no_reset",
            "write_flash"] + flash


def parse_ports(text: str) -> List[str]:
    ports = [name.strip() for name in text.split(",") if name.strip()]
    if not ports or len(set(ports)) != len(ports):
        raise FloorError(f"bad port list {text!r}")
    return ports


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


def parse_power(reply: str) -> str:
    match = re.search(r"pmu=(\w+)", reply)
    return match.group(1) if match else "none"


def read_line(port, deadline: float) -> Optional[str]:
    while time.monotonic() < deadline:
        line = port.readline().decode(errors="replace").strip()
        if line:
            return line
    return None


def read_until(port, pattern: str, deadline: float) -> Optional[str]:
    """@return the first line matching @p pattern, or None at the deadline."""
    while True:
        line = read_line(port, deadline)
        if line is None or re.search(pattern, line):
            return line


def wait_for_boot(port, timeout: float = 30.0) -> Dict[str, str]:
    port.reset()
    return wait_for_restart(port, timeout, "the node did not print its BOOT line")


def wait_for_restart(port, timeout: float = 30.0,
                     error: str = "the node did not restart") -> Dict[str, str]:
    """Waits for the next BOOT line without resetting the node."""
    line = read_until(port, r"BOOT part=", time.monotonic() + timeout)
    if line is None:
        raise FloorError(error)
    return parse_boot(line)


def command(port, line: str, timeout: float = 10.0) -> str:
    name = line.split(" ")[0]
    port.write((line + "\n").encode())
    deadline = time.monotonic() + timeout
    if read_until(port, rf"^> {re.escape(name)}$", deadline) is None:
        raise FloorError(f"no reply to {name}")
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


def has_route(port, timeout: float, poll: float = MESH_POLL_S) -> bool:
    """Polls /getRT until the routing table has an entry."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        port.write(b"/getRT\n")
        line = read_until(port, r"\(hops:|No routes|No mesher",
                          min(deadline, time.monotonic() + 5))
        if line is not None and "(hops:" in line:
            return True
        time.sleep(min(poll, max(0.0, deadline - time.monotonic())))
    return False


def check_server(port, server: Optional[str], timeout: float) -> str:
    """Opens a pull window. @return "ok" once a report is accepted, else what went wrong."""
    if server:
        command(port, f"/ota.server {server}")
    reply = command(port, f"/maint.open {CHECK_WINDOW_S}")
    if not reply.startswith("Pull window open"):
        return reply
    line = read_until(port, r"Report \w+: HTTP|No WiFi credentials|Window closed",
                      time.monotonic() + timeout)
    if line is None:
        return "no report"
    status = re.search(r"HTTP (-?\d+)", line)
    if status is None:
        return line.split(": ", 1)[-1]
    return "ok" if status.group(1).startswith("2") else f"HTTP {status.group(1)}"


def run_checks(port, boot: Dict[str, str], expected_bl: str, fingerprint: str,
               server: Optional[str], mesh_timeout: float, wifi_timeout: float) -> Dict[str, str]:
    """@return one result per name in CHECKS ("ok" or what failed) and the power chip model."""
    results = {"boot": "ok" if boot.get("state") == "VALID" else boot.get("state", "?"),
               "bl": "ok" if boot.get("bl") == expected_bl else boot.get("bl", "?")}
    pmu = parse_power(command(port, "/power"))
    results["pmu"] = "ok" if pmu != "none" else "none"
    results["key"] = "ok" if fingerprint in command(port, "/key") else "wrong key"
    results["mesh"] = "ok" if has_route(port, mesh_timeout) else "no neighbour"
    results["wifi"] = check_server(port, server, wifi_timeout)
    results["pmu_model"] = pmu
    return results


def summarize(results: Dict[str, str]) -> Tuple[str, str]:
    """@return the checks as one text and PASS or FAIL."""
    text = " ".join(f"{name}={results.get(name, '-')}" for name in CHECKS)
    passed = all(results.get(name) == "ok" for name in CHECKS)
    return text, "PASS" if passed else "FAIL"


def format_table(rows: List[Dict[str, str]]) -> str:
    header = ["port", "mac", "node", "ver", "pmu", "result", "checks"]
    lines = [header] + [[row.get(name, "") for name in header] for row in rows]
    widths = [max(len(line[i]) for line in lines) for i in range(len(header))]
    return "\n".join("  ".join(cell.ljust(width) for cell, width in zip(line, widths)).rstrip()
                     for line in lines)


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


def run_board(port_name: str, args, images: ReleaseImages, key: bytes,
              wifi: Optional[Tuple[str, str]], say: Callable[[str], None]) -> Dict[str, str]:
    """Flashes, provisions and, with --check, tests the node on @p port_name."""
    row: Dict[str, str] = {"port": port_name}
    flashed = subprocess.run([sys.executable, str(args.esptool)] +
                             esptool_args(port_name, images, not args.keep_nvs),
                             capture_output=True, text=True)
    if flashed.returncode != 0:
        raise FloorError(f"esptool failed: {(flashed.stdout + flashed.stderr)[-2000:]}")
    row["mac"] = parse_mac(flashed.stdout)
    say(f"{port_name}: flashed {args.release.name} (MAC {row['mac']})")

    port = SerialPort(port_name)
    try:
        boot = wait_for_boot(port)
        say(f"{port_name}: BOOT part={boot.get('part')} state={boot.get('state')}")
        # Give the node time to start its services and the serial console.
        time.sleep(3)
        row.update(provision(port, key, wifi, args.role))
        say(f"{port_name}: node {row['node']} provisioned, key {row['key']}")
        if args.check:
            if args.role is not None:
                boot = wait_for_restart(port)
                time.sleep(3)
            results = run_checks(port, boot, images.expected_bootloader(), row["key"],
                                 args.ota_server, args.mesh_timeout, args.wifi_timeout)
            row["pmu"] = results["pmu_model"]
            row["checks"], row["result"] = summarize(results)
            say(f"{port_name}: {row['result']} {row['checks']}")
            if row["result"] == "PASS":
                command(port, f"/identify {BLINK_S}")
    finally:
        port.close()
    return row


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("release", type=Path, help="release folder written by release.py")
    parser.add_argument("--port", required=True, help="serial port, or several separated by ,")
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--wifi-ssid", help="store this network (the password is asked for)")
    parser.add_argument("--role", choices=["sensor", "gateway"])
    parser.add_argument("--keep-nvs", action="store_true", help="do not erase NVS")
    parser.add_argument("--inventory", type=Path, default=Path("nodes.csv"))
    parser.add_argument("--esptool", type=Path, default=default_esptool())
    parser.add_argument("--check", action="store_true", help="test each node after provisioning")
    parser.add_argument("--ota-server", help="OTA server for the check, e.g. "
                        "http://loramesher.com/fw/tbeam/ (default: the image's)")
    parser.add_argument("--mesh-timeout", type=float, default=180.0,
                        help="seconds to wait for a mesh neighbour")
    parser.add_argument("--wifi-timeout", type=float, default=90.0,
                        help="seconds to wait for the report of the pull window")
    args = parser.parse_args(argv)

    try:
        ports = parse_ports(args.port)
        key = ap_pass.load_key(args.key_file)
        wifi = None
        if args.wifi_ssid:
            wifi = (args.wifi_ssid, getpass.getpass(f"Password for {args.wifi_ssid}: "))
        images = ReleaseImages.from_folder(args.release)
        images.check()
    except (FloorError, ap_pass.KeyFileError, OSError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    lock = threading.Lock()
    rows: List[Dict[str, str]] = []

    def say(text: str) -> None:
        with lock:
            print(text, flush=True)

    def worker(port_name: str) -> None:
        try:
            row = run_board(port_name, args, images, key, wifi, say)
        except (FloorError, OSError) as error:
            row = {"port": port_name, "result": "ERROR", "checks": str(error).splitlines()[0]}
            say(f"{port_name}: error: {error}")
        with lock:
            if "node" in row:
                append_inventory(args.inventory, row)
            rows.append(row)

    threads = [threading.Thread(target=worker, args=(name,)) for name in ports]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    rows.sort(key=lambda row: ports.index(row["port"]))
    print(format_table(rows))
    print(f"Inventory: {args.inventory}")
    if args.check:
        return 0 if all(row.get("result") == "PASS" for row in rows) else 1
    return 0 if all(row.get("result") != "ERROR" for row in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
