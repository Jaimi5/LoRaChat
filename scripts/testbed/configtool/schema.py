"""Parameter schema: YAML key <-> src/config.h #define symbol.

Single source of truth for what configtool can read/write. Each entry
declares how a parameter is named in YAML, what `#define` it maps to
in `src/config.h`, and how values are formatted when rendered back into
C source (which controls the suffix like `U` / `F` and string quoting).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class Param:
    yaml_key: str
    define: str
    kind: str  # "int", "uint", "float", "floatF", "hex16", "str"
    help: str

    def format_c(self, value: Any) -> str:
        """Render a YAML-provided value as the literal that appears after
        `#define NAME ` in src/config.h."""
        if self.kind == "int":
            return str(_as_int(value))
        if self.kind == "uint":
            return f"{_as_int(value)}U"
        if self.kind == "float":
            return _as_float_str(value)
        if self.kind == "floatF":
            return _as_float_str(value) + "F"
        if self.kind == "hex16":
            n = _as_int(value)
            return f"0x{n:04X}"
        if self.kind == "str":
            return f'"{value}"'
        raise ValueError(f"unknown kind {self.kind!r}")

    def parse_c(self, literal: str) -> Any:
        """Inverse of format_c: given the literal on the RHS of a #define,
        return a YAML-friendly Python value."""
        s = literal.strip()
        if self.kind == "str":
            if s.startswith('"') and s.endswith('"'):
                return s[1:-1]
            return s
        if self.kind == "uint":
            return int(s.rstrip("Uu"))
        if self.kind == "int":
            return int(s)
        if self.kind == "float":
            return float(s)
        if self.kind == "floatF":
            return float(s.rstrip("Ff"))
        if self.kind == "hex16":
            return int(s, 16) if s.lower().startswith("0x") else int(s)
        raise ValueError(f"unknown kind {self.kind!r}")


def _as_int(value: Any) -> int:
    if isinstance(value, bool):
        raise TypeError(f"bool not allowed: {value!r}")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        s = value.strip()
        if s.lower().startswith("0x"):
            return int(s, 16)
        return int(s)
    raise TypeError(f"cannot coerce {value!r} to int")


def _as_float_str(value: Any) -> str:
    if isinstance(value, (int, float)):
        # Preserve at least one decimal for readability (e.g. 125 -> "125.0")
        s = repr(float(value))
        return s
    if isinstance(value, str):
        # Verify the string parses as a float — reject garbage early rather
        # than emitting broken C. Returning the original string preserves
        # user-controlled formatting like trailing zeros (`"869.900"`).
        s = value.strip()
        float(s)  # raises ValueError for garbage; that's the point
        return s
    raise TypeError(f"cannot coerce {value!r} to float string")


PARAMS: dict[str, Param] = {p.yaml_key: p for p in [
    # --- LoRa RF + mesh identity ---------------------------------------------
    Param("lora_frequency",        "LORA_FREQUENCY",        "floatF", "Carrier frequency in MHz"),
    Param("lora_spreading_factor", "LORA_SPREADING_FACTOR", "uint",   "SF7..SF12"),
    Param("lora_bandwidth",        "LORA_BANDWIDTH",        "float",  "125.0, 250.0, or 500.0 kHz"),
    Param("lora_coding_rate",      "LORA_CODING_RATE",      "uint",   "4/5..4/8 encoded as 5..8"),
    Param("lora_power",            "LORA_POWER",            "int",    "TX power in dBm"),
    Param("lora_sync_word",        "LORA_SYNC_WORD",        "uint",   "Network identifier 0..255"),
    Param("lora_duty_cycle",       "LORA_DUTY_CYCLE",       "floatF", "Airtime duty-cycle budget 0.0..1.0"),
    Param("lora_default_data_slots", "LORA_DEFAULT_DATA_SLOTS", "uint", "Data slots each node requests per superframe (>=1); sum capped by max_network_nodes"),
    Param("lora_max_network_nodes", "LORA_MAX_NETWORK_NODES", "uint", "Max nodes admitted (node-count cap only)"),
    Param("lora_max_data_slots",   "LORA_MAX_DATA_SLOTS",    "uint", "Total data-slot pool (sum across nodes); raise above nodes*data_slots to avoid starvation"),
    Param("lora_max_packet_size",  "LORA_MAX_PACKET_SIZE",  "uint",   "Max LoRa PHY packet bytes 1..255; SF-derived if unset"),
    Param("max_msg_size",          "MAX_MSG_SIZE",          "uint",   "Max monitor message bytes = packet_size - LoRaMesher DATA overhead; derived if unset"),
    Param("lora_manager_id",       "LORA_MANAGER_ID",       "hex16",  "Mesh node ID, 16-bit hex"),
    # --- Testbed control -----------------------------------------------------
    Param("node_active",           "NODE_ACTIVE",           "uint",   "0=boot silent (no LoRa stack), 1=normal"),
    # --- WiFi + MQTT credentials ---------------------------------------------
    Param("wifi_ssid",             "WIFI_SSID",             "str",    "WiFi SSID"),
    Param("wifi_password",         "WIFI_PASSWORD",         "str",    "WiFi password"),
    Param("mqtt_server",           "MQTT_SERVER",           "str",    "MQTT broker host/IP"),
    Param("mqtt_port",             "MQTT_PORT",             "int",    "MQTT broker port"),
    Param("mqtt_username",         "MQTT_USERNAME",         "str",    "MQTT username"),
    Param("mqtt_password",         "MQTT_PASSWORD",         "str",    "MQTT password"),
    Param("mqtt_topic_sub",        "MQTT_TOPIC_SUB",        "str",    "MQTT inbound topic prefix"),
    Param("mqtt_topic_out",        "MQTT_TOPIC_OUT",        "str",    "MQTT outbound topic prefix"),
]}


# Wire overhead of a LoRaMesher v2 DATA packet: 6-byte BaseHeader
# (dst+src+type+payload_size) + 4-byte DataHeader (next_hop+ttl+seq_num).
# Usable application payload = max_packet_size - LORAMESHER_DATA_OVERHEAD.
LORAMESHER_DATA_OVERHEAD = 10


def max_packet_size_for_sf(sf: int, bw_khz: float) -> int:
    """SF/bandwidth -> max LoRa PHY packet size in bytes.

    Mirrors LoRaMesher's RadioConfig::GetMaxPacketSizeForSf: a BW125 base per
    SF, doubled at 250 kHz and quadrupled at 500 kHz, clamped to 1..255.
    """
    base = {7: 242, 8: 242, 9: 115}.get(int(sf), 51)
    if bw_khz >= 500.0 - 0.1:
        scaled = base * 4
    elif bw_khz >= 250.0 - 0.1:
        scaled = base * 2
    else:
        scaled = base
    return max(1, min(255, scaled))


def param(key: str) -> Param:
    """Look up a Param by YAML key; raise KeyError with a helpful message."""
    try:
        return PARAMS[key]
    except KeyError:
        known = ", ".join(sorted(PARAMS))
        raise KeyError(f"unknown parameter {key!r}. Known: {known}") from None


def param_by_define(define: str) -> Param | None:
    """Reverse lookup: find the Param whose #define matches."""
    for p in PARAMS.values():
        if p.define == define:
            return p
    return None
