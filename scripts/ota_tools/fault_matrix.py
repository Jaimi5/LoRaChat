#!/usr/bin/env python3
"""Runs the boot-guard fault images on bench nodes and checks every rollback.

    python fault_matrix.py --floor release\\tbeam\\0.1.2+gabc1234 --images release\\tbeam ^
        --sensors COM6,COM7,COM9 [--gateway COM3] --key-file deploy.key ^
        --server-url http://192.168.1.26:8070/ [--kinds 1,2,3,4] [--runs 3]

For each run and fault kind (1 crash, 2 irq-hang, 3 loop-hang, 4 selftest-fail, built by
build_test_images.sh into <images>/<version>-t<kind>/): every node gets the floor image again
(flash_floor.py, so the blacklist is empty), the bench server serves the fault image and takes
the reports, and each node opens a pull window (a gateway checks over its own WiFi). The serial
log is followed until the node refuses the image as blacklisted; a node that stops before (a
gateway after a rollback) gets a new window. Then the log and the reports are judged:

- crash, irq-hang: two installs, two unexplained rollbacks (reset PANIC or INT_WDT), the second
  blacklists the image;
- loop-hang, selftest-fail: one install, one rollback with reason 6 (deadline) or 5 (OTA path),
  blacklisted at once;
- every install and every rollback reported, one noop for the refusal, the node back on a valid
  image.

A table per run is printed and all results are written to a JSON file. The exit code is 0
only if every node passes every kind in every run. Run with Windows Python (pyserial).
"""
import argparse
import json
import re
import sys
import threading
import time
from argparse import Namespace
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib.parse import urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ap_pass  # noqa: E402
import flash_floor  # noqa: E402
import fw_server  # noqa: E402

Event = Tuple

SENSOR_WINDOW_S = 900
GATEWAY_WINDOW_S = 60
REOPEN_AFTER_S = 25.0
STALL_S = 90.0
BOOT_SETTLE_S = 20.0
KIND_TIMEOUT_S = 900.0


@dataclass(frozen=True)
class Kind:
    number: int
    name: str
    rr: str
    reason: int
    attempts: int


KINDS = {
    1: Kind(1, "crash", "PANIC", 0, 2),
    2: Kind(2, "irq-hang", "INT_WDT", 0, 2),
    3: Kind(3, "loop-hang", "SW", 6, 1),
    4: Kind(4, "selftest-fail", "SW", 5, 1),
}

PATTERNS = [
    (re.compile(r"BOOT part=(\w+) state=(\w+) .*rr=(\w+)"),
     lambda m: ("boot", m.group(1), m.group(2), m.group(3))),
    (re.compile(r"Update attempt rolled back, reason (\d+), unexplained (\d+)(, blacklisted)?"),
     lambda m: ("rolled_back", int(m.group(1)), int(m.group(2)), m.group(3) is not None)),
    (re.compile(r"Not installing it: (\w+)"), lambda m: ("refused", m.group(1))),
    (re.compile(r"Report (\w+): HTTP (-?\d+)"), lambda m: ("report", m.group(1), int(m.group(2)))),
    (re.compile(r"Image OK after \d+ attempt"), lambda m: ("installed",)),
    (re.compile(r"Manifest for "), lambda m: ("manifest",)),
    (re.compile(r"Window closed after"), lambda m: ("window_closed",)),
]


def parse_event(line: str) -> Optional[Event]:
    for pattern, make in PATTERNS:
        match = pattern.search(line)
        if match:
            return make(match)
    return None


def accepted_reports(events: List[Event], name: str) -> int:
    return sum(1 for e in events if e[0] == "report" and e[1] == name and 200 <= e[2] < 300)


def judge(kind: Kind, events: List[Event]) -> List[str]:
    """@return what went wrong for @p kind in the node's log events; empty if it passed."""
    problems = []
    installed = sum(1 for e in events if e[0] == "installed")
    if installed != kind.attempts:
        problems.append(f"{kind.attempts} installs expected, {installed} done")

    boots = [e for e in events if e[0] == "boot"]
    for before, after in zip(boots, boots[1:]):
        if before[2] == "PENDING_VERIFY" and after[2] == "VALID" and after[3] != kind.rr:
            problems.append(f"reset reason {after[3]} after the fault, expected {kind.rr}")

    rollbacks = [e for e in events if e[0] == "rolled_back"]
    if len(rollbacks) != kind.attempts:
        problems.append(f"{kind.attempts} rollbacks expected, {len(rollbacks)} seen")
    for rollback in rollbacks:
        if rollback[1] != kind.reason:
            problems.append(f"rollback reason {rollback[1]}, expected {kind.reason}")
    if rollbacks and not rollbacks[-1][3]:
        problems.append("the last rollback did not blacklist the image")
    if any(rollback[3] for rollback in rollbacks[:-1]):
        problems.append("the image was blacklisted before its last rollback")
    if not any(e[0] == "refused" and e[1] == "SKIP_BLACKLISTED" for e in events):
        problems.append("the image was never refused as blacklisted")

    for name in ("written", "rollback"):
        count = accepted_reports(events, name)
        if count != kind.attempts:
            problems.append(f"{kind.attempts} {name} reports expected, {count} accepted")
    if accepted_reports(events, "noop") == 0:
        problems.append("no noop report accepted")
    if boots and boots[-1][2] != "VALID":
        problems.append(f"the node ended in {boots[-1][2]}")
    return problems


def judge_server(kind: Kind, reports: List[dict], node: str) -> List[str]:
    """@return what is missing in the reports the bench server received from @p node."""
    events = [report.get("event") for report in reports if report.get("node") == node]
    problems = []
    for name in ("written", "rollback"):
        count = events.count(name)
        if count != kind.attempts:
            problems.append(f"server: {kind.attempts} {name} reports expected, {count} received")
    if "noop" not in events:
        problems.append("server: no noop report")
    return problems


def fault_release(images: Path, kind: int) -> Path:
    """@return the newest <images>/<version>-t<kind> folder."""
    folders = sorted(images.glob(f"*-t{kind}"), key=lambda path: path.stat().st_mtime)
    if not folders:
        raise flash_floor.FloorError(f"no fault image t{kind} in {images}")
    return folders[-1]


def drive(port, gateway: bool, deadline: float, say) -> List[Event]:
    """Opens windows until the node refuses the image as blacklisted. @return the log events."""
    window = GATEWAY_WINDOW_S if gateway else SENSOR_WINDOW_S
    port.write(f"/maint.open {window}\n".encode())
    events: List[Event] = []
    idle_since: Optional[float] = None
    refused_at: Optional[float] = None
    last_event_at = time.monotonic()
    pending = False
    while time.monotonic() < deadline:
        line = flash_floor.read_line(port, min(deadline, time.monotonic() + 1.0))
        now = time.monotonic()
        event = parse_event(line) if line else None
        if event is not None:
            events.append(event)
            last_event_at = now
            if event[0] == "boot":
                pending = event[2] == "PENDING_VERIFY"
            if event[0] == "refused" and event[1] == "SKIP_BLACKLISTED":
                refused_at = now
            if event[0] == "report" and event[1] == "rollback":
                idle_since = now
            if event[0] in ("manifest", "boot"):
                idle_since = None
            if event[0] == "window_closed" and refused_at is None:
                idle_since = now
        if refused_at is not None and (now - refused_at > 10 or
                                       accepted_reports(events, "noop") > 0):
            break
        stalled = not pending and now - last_event_at > STALL_S
        if (idle_since is not None and now - idle_since > REOPEN_AFTER_S) or stalled:
            say(f"opening a new window ({window} s)")
            port.write(f"/maint.open {window}\n".encode())
            idle_since = None
            last_event_at = now
    return events


def run_kind(kind: Kind, release: Path, nodes: Dict[str, str], args, key: bytes,
             say_all) -> Dict[str, dict]:
    """Flashes the floor on every node, then runs one fault image. @return results by port."""
    reports: List[dict] = []
    reports_lock = threading.Lock()

    def on_report(fields: dict) -> None:
        with reports_lock:
            reports.append(fields)

    url = urlparse(args.server_url)
    server = fw_server.make_server(release, url.port or 80, fw_server.Faults(),
                                   deployment_key=key, on_report=on_report)
    ready = threading.Barrier(len(nodes) + 1)
    results: Dict[str, dict] = {}

    def worker(port_name: str, role: str) -> None:
        def say(text: str) -> None:
            say_all(f"{port_name} t{kind.number}: {text}")

        result = {"node": "", "problems": [], "events": []}
        port = None
        try:
            floor_args = Namespace(release=args.floor, esptool=args.esptool, keep_nvs=False,
                                   role=role, check=False)
            row = flash_floor.run_board(port_name, floor_args, flash_floor.ReleaseImages.from_folder(
                args.floor), key, None, say)
            result["node"] = row["node"]
            port = flash_floor.SerialPort(port_name)
            try:
                flash_floor.wait_for_restart(port, timeout=15)
            except flash_floor.FloorError:
                pass  # The restart after /role.set may be over before the port opens.
            time.sleep(BOOT_SETTLE_S)
            flash_floor.command(port, f"/ota.server {args.server_url}")
        except (flash_floor.FloorError, OSError) as error:
            result["problems"].append(f"setup: {error}")
        try:
            ready.wait(timeout=600)
        except threading.BrokenBarrierError:
            result["problems"].append("setup: the other nodes were not ready")
        if port is not None and not result["problems"]:
            try:
                events = drive(port, role == "gateway",
                               time.monotonic() + KIND_TIMEOUT_S, say)
                result["events"] = [list(event) for event in events]
                result["problems"] += judge(kind, events)
            except OSError as error:
                result["problems"].append(f"serial: {error}")
        if port is not None:
            port.close()
        results[port_name] = result

    threads = [threading.Thread(target=worker, args=(name, role)) for name, role in nodes.items()]
    for thread in threads:
        thread.start()
    try:
        ready.wait(timeout=600)
    except threading.BrokenBarrierError:
        pass
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    say_all(f"t{kind.number} {kind.name}: serving {release.name}")
    for thread in threads:
        thread.join()
    server.shutdown()
    server.server_close()

    for result in results.values():
        if result["node"]:
            result["problems"] += judge_server(kind, reports, result["node"])
    return results


def format_run(run: int, table: Dict[int, Dict[str, dict]], ports: List[str]) -> str:
    lines = [f"Run {run}"]
    for number, results in table.items():
        for port in ports:
            result = results.get(port, {"node": "", "problems": ["not run"]})
            verdict = "PASS" if not result["problems"] else "FAIL"
            detail = "; ".join(result["problems"])
            lines.append(f"  t{number} {KINDS[number].name:<13} {port:<6} {result['node']:<5} "
                         f"{verdict} {detail}".rstrip())
    return "\n".join(lines)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--floor", type=Path, required=True, help="floor release folder")
    parser.add_argument("--images", type=Path, required=True,
                        help="folder with the <version>-t<kind> fault releases")
    parser.add_argument("--sensors", default="", help="sensor ports, separated by ,")
    parser.add_argument("--gateway", help="gateway port")
    parser.add_argument("--key-file", type=Path, required=True)
    parser.add_argument("--server-url", required=True,
                        help="this PC as the nodes reach it, e.g. http://192.168.1.26:8070/")
    parser.add_argument("--kinds", default="1,2,3,4")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--results", type=Path, default=Path("fault_matrix.json"))
    parser.add_argument("--esptool", type=Path, default=flash_floor.default_esptool())
    args = parser.parse_args(argv)

    try:
        key = ap_pass.load_key(args.key_file)
        nodes = {name: "sensor" for name in flash_floor.parse_ports(args.sensors)} \
            if args.sensors else {}
        if args.gateway:
            nodes[args.gateway] = "gateway"
        if not nodes:
            raise flash_floor.FloorError("give --sensors and/or --gateway")
        kinds = [KINDS[int(number)] for number in args.kinds.split(",")]
        releases = {kind.number: fault_release(args.images, kind.number) for kind in kinds}
    except (flash_floor.FloorError, ap_pass.KeyFileError, KeyError, ValueError, OSError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1

    lock = threading.Lock()

    def say(text: str) -> None:
        with lock:
            print(f"{time.strftime('%H:%M:%S')} {text}", flush=True)

    all_runs = []
    passed = True
    for run in range(1, args.runs + 1):
        table: Dict[int, Dict[str, dict]] = {}
        for kind in kinds:
            table[kind.number] = run_kind(kind, releases[kind.number], nodes, args, key, say)
        all_runs.append({str(k): v for k, v in table.items()})
        print(format_run(run, table, list(nodes)), flush=True)
        passed &= all(not result["problems"] for results in table.values()
                      for result in results.values())
        args.results.write_text(json.dumps(all_runs, indent=1))
    print(f"{'PASS' if passed else 'FAIL'}: {args.runs} run(s); results in {args.results}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
