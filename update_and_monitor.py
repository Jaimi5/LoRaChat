from platformio import util
import argparse
import atexit
import subprocess
import sys
import threading
from datetime import datetime

monitor_procs = []
monitor_procs_lock = threading.Lock()


def list_ports():
    ports = util.get_serial_ports()
    if not ports:
        print("No connected serial ports found.")
        return
    print("Connected serial ports:")
    for p in ports:
        desc = p.get("description", "").strip() or "Unknown"
        print(f"  {p['port']:<8} - {desc}")


def sanitize_port(port):
    # On Linux /dev/ttyUSB0 -> ttyUSB0, on Windows COM9 stays COM9
    return port.replace("/dev/", "").replace("\\", "_")


def build_env(env):
    print(f"[build] Compiling environment: {env}")
    result = subprocess.run(["pio", "run", "-e", env], text=True)
    if result.returncode != 0:
        print(f"[build] FAILED for environment: {env}", file=sys.stderr)
        return False
    print(f"[build] OK: {env}")
    return True


def upload_and_monitor(port, env):
    print(f"[{port}] Uploading {env} ...")
    result = subprocess.run(
        ["pio", "run", "-e", env, "--target", "upload", "--upload-port", port],
        text=True,
    )
    if result.returncode != 0:
        print(f"[{port}] Upload FAILED", file=sys.stderr)
        return

    print(f"[{port}] Upload OK, starting monitor")

    timestamp = datetime.now().strftime("%H%M%S")
    safe_port = sanitize_port(port)
    log_filename = f"monitor_{timestamp}_{safe_port}_{env}.ans"

    with open(log_filename, "wb") as log_file:
        proc = subprocess.Popen(
            [
                "pio",
                "device",
                "monitor",
                "--environment",
                env,
                "--port",
                port,
                "--filter",
                "esp32_exception_decoder",
                "--filter",
                "time",
            ],
            stdout=log_file,
            stderr=log_file,
        )
        with monitor_procs_lock:
            monitor_procs.append(proc)
        print(f"[{port}] Monitor log: {log_filename}")
        proc.wait()


def cleanup():
    with monitor_procs_lock:
        procs = list(monitor_procs)
    for proc in procs:
        try:
            proc.terminate()
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(
        description="Build, upload, and monitor multiple PlatformIO devices in parallel."
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List connected serial ports and exit.",
    )
    parser.add_argument(
        "targets",
        nargs="*",
        metavar="PORT:ENV",
        help="One or more PORT:ENV pairs, e.g. COM9:ttgo-t-beam-v2",
    )
    args = parser.parse_args()

    if args.list:
        list_ports()
        return

    if not args.targets:
        parser.print_help()
        sys.exit(1)

    pairs = []
    for target in args.targets:
        parts = target.split(":", 1)
        if len(parts) != 2 or not parts[0] or not parts[1]:
            print(f"Error: malformed target '{target}'. Expected PORT:ENV format.", file=sys.stderr)
            sys.exit(1)
        pairs.append((parts[0], parts[1]))

    # Deduplicated build phase (sequential)
    seen_envs = []
    for _, env in pairs:
        if env not in seen_envs:
            seen_envs.append(env)

    for env in seen_envs:
        if not build_env(env):
            sys.exit(1)

    print("All builds succeeded. Starting uploads...")

    atexit.register(cleanup)

    threads = []
    for port, env in pairs:
        t = threading.Thread(target=upload_and_monitor, args=(port, env), daemon=True)
        threads.append(t)
        t.start()

    try:
        for t in threads:
            t.join()
    except KeyboardInterrupt:
        print("\nInterrupted. Terminating monitor processes...")
        cleanup()


if __name__ == "__main__":
    main()
