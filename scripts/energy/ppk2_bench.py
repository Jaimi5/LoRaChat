#!/usr/bin/env python3
"""Records a PPK2 current trace together with the node and gateway serial logs.

    python ppk2_bench.py --test E3 --board COM3 --seconds 900 --voltage 5.0 ^
        [--node-port COM3] [--gateway-port COM7] [--power-cycle 5] ^
        [--at "30:@5E9C /displayOn"] [--every "30:@5E9C /version"] [--raw]

The PPK2 runs as source meter at --voltage. With --power-cycle S the board is switched off for
S seconds first and the recording starts before it is switched on again, so boot and the boot
window are in the trace. The PPK2 switches its output off when the program ends, so every run
starts with a boot; --warmup S powers the board S seconds before the recording starts (steady
state tests). --at T:LINE sends LINE to the gateway console once at T seconds,
--every P:LINE every P seconds (signed lines come from lmcmd.py sign). Everything shares one PC
clock: log lines are written as "<unix time>\\t<line>", and trace.csv and meta.json hold the unix
time of the first sample ("epoch"), so line time - epoch is the position in the trace.

Output: <out>/<date>_<board>_<test>_<voltage>/ with trace.csv (1 ms mean and max bins, read by
ppk2_report.py), node.log, gateway.log, meta.json and with --raw raw.f32 (every sample in uA,
float32). Close the nRF Power Profiler app first: only one program can open the PPK2.
Run with Windows Python (pyserial, ppk2-api); standard library otherwise.
"""
import argparse
import datetime
import json
import sys
import threading
import time
from array import array
from pathlib import Path
from typing import Callable, List, Optional, Tuple

SAMPLE_RATE = 100_000
BIN_SAMPLES = 100
BAUD = 115200
CAPTURE_TAG = "# ppk2_bench capture:"
DEFAULT_OUT = Path(__file__).resolve().parents[2] / "release" / "bench" / "energy"


def capture_name(date: str, board: str, test: str, voltage: float) -> str:
    return f"{date}_{board}_{test}_{voltage:.1f}".replace(".", "v")


class Binner:
    """Reduces a sample stream to bins of @p samples_per_bin: mean and max per bin."""

    def __init__(self, samples_per_bin: int = BIN_SAMPLES):
        self.size = samples_per_bin
        self.mean: List[float] = []
        self.max: List[float] = []
        self._sum = 0.0
        self._max = float("-inf")
        self._count = 0

    def add(self, samples) -> None:
        for value in samples:
            self._sum += value
            if value > self._max:
                self._max = value
            self._count += 1
            if self._count == self.size:
                self._close()

    def flush(self) -> None:
        if self._count:
            self._close()

    def _close(self) -> None:
        self.mean.append(self._sum / self._count)
        self.max.append(self._max)
        self._sum, self._max, self._count = 0.0, float("-inf"), 0


def _timed(text: str) -> Tuple[float, str]:
    seconds, sep, line = text.partition(":")
    try:
        value = float(seconds)
    except ValueError:
        value = None
    if not sep or value is None or not line.strip():
        raise ValueError(f"expected SECONDS:LINE, got {text!r}")
    return value, line.strip()


class Schedule:
    """Gateway lines to send once (--at) or periodically (--every)."""

    def __init__(self, at: List[str], every: List[str]):
        self.once = sorted(_timed(text) for text in at)
        self.periodic = [[period, line, period] for period, line in map(_timed, every)]

    def due(self, elapsed: float) -> List[str]:
        """@return the lines due at @p elapsed seconds, in time order."""
        due: List[Tuple[float, str]] = []
        while self.once and self.once[0][0] <= elapsed:
            due.append(self.once.pop(0))
        for entry in self.periodic:
            period, line, next_at = entry
            while next_at <= elapsed:
                due.append((next_at, line))
                next_at += period
            entry[2] = next_at
        return [line for _, line in sorted(due, key=lambda item: item[0])]


class SerialLog:
    """Logs a serial port's lines with the capture clock; reopens the port when it comes back
    (a board powered by the PPK2 loses its USB-serial chip while switched off)."""

    def __init__(self, name: str, path: Path, clock: Callable[[], float] = time.time):
        self.name = name
        self.clock = clock
        self.out = open(path, "w", encoding="utf-8")
        self.port = None
        self.lock = threading.Lock()
        self.stopping = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _open(self):
        import serial
        port = serial.Serial()
        port.port = self.name
        port.baudrate = BAUD
        port.timeout = 0.2
        port.dtr = False
        port.rts = False
        port.open()
        return port

    def _write(self, line: str) -> None:
        with self.lock:
            self.out.write(f"{self.clock():.3f}\t{line}\n")
            self.out.flush()

    def _run(self) -> None:
        while not self.stopping.is_set():
            try:
                if self.port is None:
                    self.port = self._open()
                    self._write(f"## {self.name} open")
                raw = self.port.readline()
                if raw:
                    self._write(raw.decode(errors="replace").rstrip("\r\n"))
            except OSError:
                if self.port is not None:
                    self._write(f"## {self.name} lost")
                    try:
                        self.port.close()
                    except OSError:
                        pass
                    self.port = None
                time.sleep(0.5)

    def send(self, line: str) -> None:
        self._write(f"> {line}")
        try:
            if self.port is not None:
                self.port.write((line + "\n").encode())
                return
        except OSError:
            pass
        self._write(f"## not sent, {self.name} is closed")

    def close(self) -> None:
        self.stopping.set()
        self.thread.join(timeout=2)
        if self.port is not None:
            self.port.close()
        self.out.close()


def capture(ppk2, folder: Path, seconds: float, voltage: float, power_cycle_s: float = 0.0,
            warmup_s: float = 0.0, schedule: Optional[Schedule] = None, gateway=None,
            raw: bool = False,
            clock: Callable[[], float] = time.time,
            sleep: Callable[[float], None] = time.sleep) -> dict:
    """Records @p seconds of current from @p ppk2 into @p folder. @return the metadata."""
    folder.mkdir(parents=True, exist_ok=True)
    ppk2.get_modifiers()
    ppk2.use_source_meter()
    ppk2.set_source_voltage(int(round(voltage * 1000)))
    if power_cycle_s > 0:
        ppk2.toggle_DUT_power("OFF")
        sleep(power_cycle_s)
    binner = Binner()
    raw_out = open(folder / "raw.f32", "wb") if raw else None
    samples = 0
    if warmup_s > 0:
        ppk2.toggle_DUT_power("ON")
        sleep(warmup_s)
        ppk2.start_measuring()
        epoch = clock()
    else:
        ppk2.start_measuring()
        epoch = clock()
        ppk2.toggle_DUT_power("ON")
    try:
        while samples < seconds * SAMPLE_RATE:
            if schedule is not None and gateway is not None:
                for line in schedule.due(samples / SAMPLE_RATE):
                    gateway.send(line)
            data = ppk2.get_data()
            if not data:
                sleep(0.001)
                continue
            batch = ppk2.get_samples(data)
            if isinstance(batch, tuple):
                batch = batch[0]
            batch = batch[:max(0, int(seconds * SAMPLE_RATE) - samples)]
            binner.add(batch)
            if raw_out is not None:
                array("f", batch).tofile(raw_out)
            samples += len(batch)
    finally:
        ppk2.stop_measuring()
        if raw_out is not None:
            raw_out.close()
    binner.flush()

    with open(folder / "trace.csv", "w") as out:
        out.write(f"{CAPTURE_TAG} bin_s={BIN_SAMPLES / SAMPLE_RATE:g} epoch={epoch:.3f} "
                  f"voltage={voltage:.3f}\nt_s,mean_uA,max_uA\n")
        for index, (mean, peak) in enumerate(zip(binner.mean, binner.max)):
            out.write(f"{index * BIN_SAMPLES / SAMPLE_RATE:.6f},{mean:.1f},{peak:.1f}\n")
    meta = {"epoch": epoch, "seconds": seconds, "samples": samples, "voltage": voltage,
            "power_cycle_s": power_cycle_s, "warmup_s": warmup_s,
            "wall_clock_s": clock() - epoch}
    (folder / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")
    return meta


def open_ppk2(port: Optional[str]):
    from ppk2_api.ppk2_api import PPK2_API

    if port is None:
        devices = PPK2_API.list_devices()
        if not devices:
            raise SystemExit("No PPK2 found (is the nRF Power Profiler app still open?)")
        port = devices[0][0] if isinstance(devices[0], (tuple, list)) else devices[0]
    return PPK2_API(port, timeout=1, write_timeout=1, exclusive=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--test", required=True, help="test name, e.g. E3")
    parser.add_argument("--board", required=True, help="board label for the folder, e.g. COM3")
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--voltage", type=float, default=5.0)
    parser.add_argument("--power-cycle", type=float, default=0.0,
                        help="switch the board off for this many seconds first")
    parser.add_argument("--warmup", type=float, default=0.0,
                        help="power the board this many seconds before recording")
    parser.add_argument("--node-port", help="measured node's serial (VBUS-cut cable)")
    parser.add_argument("--gateway-port", help="gateway console for --at/--every lines")
    parser.add_argument("--at", action="append", default=[], metavar="SECONDS:LINE")
    parser.add_argument("--every", action="append", default=[], metavar="SECONDS:LINE")
    parser.add_argument("--ppk2-port", help="PPK2 serial port (default: first found)")
    parser.add_argument("--raw", action="store_true", help="also keep every sample")
    parser.add_argument("--note", default="", help="free text stored in meta.json")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args(argv)

    schedule = Schedule(args.at, args.every)
    if (args.at or args.every) and not args.gateway_port:
        parser.error("--at/--every need --gateway-port")
    name = capture_name(datetime.date.today().isoformat(), args.board, args.test, args.voltage)
    folder = args.out / name
    folder.mkdir(parents=True, exist_ok=True)

    ppk2 = open_ppk2(args.ppk2_port)
    node = SerialLog(args.node_port, folder / "node.log") if args.node_port else None
    gateway = SerialLog(args.gateway_port, folder / "gateway.log") if args.gateway_port else None
    logs = [log for log in (node, gateway) if log is not None]

    print(f"Recording {args.seconds:.0f} s into {folder}")
    try:
        meta = capture(ppk2, folder, args.seconds, args.voltage, args.power_cycle, args.warmup,
                       schedule, gateway, args.raw)
    finally:
        for log in logs:
            log.close()
    meta.update({"test": args.test, "board": args.board, "node_port": args.node_port,
                 "gateway_port": args.gateway_port, "at": args.at, "every": args.every,
                 "note": args.note})
    (folder / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")
    print(f"Done: {meta['samples']} samples in {meta['wall_clock_s']:.1f} s; "
          f"python3 scripts/energy/ppk2_report.py release/bench/energy/{folder.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
