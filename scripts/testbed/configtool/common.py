"""Shared helpers: repo paths, testbed.conf loader, YAML loader."""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from schema import LORAMESHER_DATA_OVERHEAD, max_packet_size_for_sf


class HexInt(int):
    """An int that dumps as `0xNNNN` in YAML. Round-trips fine: PyYAML's
    YAML 1.1 resolver parses `0xNNNN` scalars back into plain int, so reading
    a file written by us produces normal ints — only the display differs."""
    __slots__ = ()


def _hex_int_representer(dumper: yaml.Dumper, data: HexInt):
    return dumper.represent_scalar("tag:yaml.org,2002:int", f"0x{int(data):04X}")


yaml.add_representer(HexInt, _hex_int_representer, Dumper=yaml.SafeDumper)


# Repo root is three levels up from this file: <repo>/scripts/testbed/configtool/<x>.py
REPO_ROOT = Path(__file__).resolve().parents[3]
TESTBED_DIR = REPO_ROOT / "scripts" / "testbed"
TESTBED_CONF = TESTBED_DIR / "testbed.conf"
EXPERIMENTS_DIR = TESTBED_DIR / "experiments"
CHANGE_CONFIG_DIR = TESTBED_DIR / "change-config"
CONFIG_H = REPO_ROOT / "src" / "config.h"


@dataclass
class TestbedConf:
    """Parsed view of testbed.conf. Only fields we actually use here."""
    gw_ssh: dict[str, str] = field(default_factory=dict)
    gw_pass: dict[str, str] = field(default_factory=dict)
    gw_key: dict[str, str] = field(default_factory=dict)
    gw_port: dict[str, str] = field(default_factory=dict)
    devices: list[tuple[str, str]] = field(default_factory=list)  # (device_id, gw_id)
    repo_path: str = "/home/lora/LoRaChat"
    serial_prefix: str = "/home/lora/dev/lora-"
    ssh_opts: str = ""

    def short_ids(self) -> list[str]:
        """Last hex chunk of each device_id — the SHORT_ID used in script names."""
        return [device_id.rsplit("-", 1)[-1] for device_id, _ in self.devices]

    def device_gw(self, short_id: str) -> str | None:
        for device_id, gw in self.devices:
            if device_id.rsplit("-", 1)[-1] == short_id:
                return gw
        return None


def load_testbed_conf(path: Path = TESTBED_CONF) -> TestbedConf:
    """Source testbed.conf from bash and read back the associative arrays +
    DEVICES as JSON on stdout. Sourcing bash is the safest way to handle
    `declare -A` without reimplementing a bash parser.

    Keys like `GW-1` aren't valid shell identifiers, so instead of exporting
    them as env vars we emit JSON directly from bash."""
    if not path.exists():
        raise FileNotFoundError(f"testbed.conf not found at {path}")

    program = r'''
set -euo pipefail
source "$1"

json_escape() {
    # Minimal JSON string escaper for shell strings.
    local s=${1//\\/\\\\}
    s=${s//\"/\\\"}
    s=${s//$'\n'/\\n}
    s=${s//$'\r'/\\r}
    s=${s//$'\t'/\\t}
    printf '"%s"' "$s"
}

emit_map() {
    local -n m=$1
    local first=1
    printf '{'
    for k in "${!m[@]}"; do
        [[ $first -eq 1 ]] || printf ','
        first=0
        printf '%s:%s' "$(json_escape "$k")" "$(json_escape "${m[$k]}")"
    done
    printf '}'
}

printf '{'
printf '"gw_ssh":';  emit_map GW_SSH
printf ',"gw_pass":'; emit_map GW_PASS
printf ',"gw_key":';  emit_map GW_KEY
printf ',"gw_port":'; emit_map GW_PORT
printf ',"devices":['
first=1
for d in "${DEVICES[@]}"; do
    [[ $first -eq 1 ]] || printf ','
    first=0
    printf '%s' "$(json_escape "$d")"
done
printf ']'
printf ',"repo_path":%s' "$(json_escape "${REPO_PATH:-/home/lora/LoRaChat}")"
printf ',"serial_prefix":%s' "$(json_escape "${SERIAL_PORT_PREFIX:-/home/lora/dev/lora-}")"
printf ',"ssh_opts":%s' "$(json_escape "${SSH_OPTS:-}")"
printf '}\n'
'''
    result = subprocess.run(
        ["bash", "-c", program, "bash", str(path)],
        capture_output=True, text=True, check=True,
    )
    data = json.loads(result.stdout.strip())

    conf = TestbedConf(
        gw_ssh=data["gw_ssh"],
        gw_pass=data["gw_pass"],
        gw_key=data["gw_key"],
        gw_port=data["gw_port"],
        repo_path=data["repo_path"],
        serial_prefix=data["serial_prefix"],
        ssh_opts=data["ssh_opts"],
    )
    for line in data["devices"]:
        line = line.split("#", 1)[0].strip().strip('"').strip("'")
        if not line or ":" not in line:
            continue
        device_id, gw = line.split(":", 1)
        conf.devices.append((device_id.strip(), gw.strip()))
    return conf


def load_yaml(path: Path) -> dict[str, Any]:
    with open(path) as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        raise ValueError(f"{path}: YAML root must be a mapping")
    return data


def apply_derived_defaults(params: dict[str, Any]) -> dict[str, Any]:
    """Fill SF-derived packet/message sizes when not explicitly set.

    Mutates and returns `params`. If the spreading factor is known, an unset
    `lora_max_packet_size` is derived from (SF, bandwidth) to mirror
    LoRaMesher's own default, and an unset `max_msg_size` is set to
    `packet_size - LORAMESHER_DATA_OVERHEAD`. Explicit values are preserved.
    """
    sf = params.get("lora_spreading_factor")
    if sf is None:
        return params

    bw = float(params.get("lora_bandwidth", 125.0))
    if params.get("lora_max_packet_size") is None:
        params["lora_max_packet_size"] = max_packet_size_for_sf(int(sf), bw)

    if params.get("max_msg_size") is None:
        packet_size = int(params["lora_max_packet_size"])
        params["max_msg_size"] = max(1, packet_size - LORAMESHER_DATA_OVERHEAD)

    return params


def flatten(experiment: dict[str, Any]) -> dict[str, dict[str, Any]]:
    """Apply `defaults` to every device, returning {short_id: {param: value}}.

    Per-device keys override defaults. SF-derived packet/message sizes are
    filled in per device (see `apply_derived_defaults`). Unknown sections
    (e.g. `description`) are ignored.
    """
    defaults = experiment.get("defaults") or {}
    devices = experiment.get("devices") or {}
    flat: dict[str, dict[str, Any]] = {}
    for short_id, overrides in devices.items():
        merged = dict(defaults)
        if overrides:
            merged.update(overrides)
        flat[str(short_id)] = apply_derived_defaults(merged)
    return flat
