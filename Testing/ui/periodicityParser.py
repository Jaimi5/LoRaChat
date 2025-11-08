"""
Message Periodicity Parser
Extracts message events from ESP32 LoRa .ans log files for periodicity analysis
"""

import re
from dataclasses import dataclass
from typing import List, Optional, Dict
from datetime import datetime, timedelta


# LoRaMesh packet type flags from BuildOptions.h
# These are binary flags that can be combined
NEED_ACK_P = 0b00000011  # 3 - Flag indicating packet needs acknowledgment
DATA_P     = 0b00000010  # 2 - Basic data packet
HELLO_P    = 0b00000100  # 4 - Hello/discovery packet
ACK_P      = 0b00001010  # 10 - Acknowledgment packet
XL_DATA_P  = 0b00010010  # 18 - Extra-large data packet (multi-part)
LOST_P     = 0b00100010  # 34 - Lost packet notification
SYNC_P     = 0b01000010  # 66 - Synchronization packet


def decode_packet_type(type_value: int) -> str:
    """
    Decode packet type bitmask into human-readable name.

    Packet types can be combined (e.g., XL_DATA_P | NEED_ACK_P).
    Bit 0 (0x01) typically indicates NEED_ACK flag.

    Args:
        type_value: Integer bitmask from log file

    Returns:
        Human-readable packet type string (e.g., "HELLO", "XL_DATA+ACK")
    """
    # Check exact matches first (packets without ACK flag)
    if type_value == HELLO_P:
        return "HELLO"
    if type_value == ACK_P:
        return "ACK"
    if type_value == SYNC_P:
        return "SYNC"
    if type_value == LOST_P:
        return "LOST"

    # Check for data packet types
    # XL_DATA_P has more bits set, check it first
    base = None
    if type_value & XL_DATA_P == XL_DATA_P:
        base = "XL_DATA"
    elif type_value & DATA_P == DATA_P:
        base = "DATA"

    # Check if NEED_ACK flag is set (bit 0)
    if base:
        if type_value & 0b00000001:
            return f"{base}+ACK"
        else:
            return base

    # Unknown type
    return f"UNKNOWN_0x{type_value:02X}"


def get_all_packet_type_names() -> List[str]:
    """
    Get list of all known packet type names for UI filters.

    Returns:
        List of packet type names
    """
    return [
        "HELLO",
        "DATA",
        "DATA+ACK",
        "XL_DATA",
        "XL_DATA+ACK",
        "ACK",
        "LOST",
        "SYNC",
    ]


@dataclass
class MessageEvent:
    """Represents a single message/packet event"""
    timestamp: str  # HH:MM:SS.mmm format
    uptime_ms: int  # Device uptime in milliseconds
    device_id: str  # Hex address (e.g., "571C")
    com_port: str  # COM port (e.g., "COM10")
    direction: str  # "send" or "received"
    src_addr: str  # Source address (hex)
    dst_addr: str  # Destination address (hex)
    packet_type: int  # Packet type number
    packet_type_name: str  # Human-readable type name
    size: int  # Packet size in bytes
    packet_id: int  # Packet ID
    via: str  # Via address (hex)
    seq_id: int  # Sequence ID
    num: int  # Packet number

    def get_time_seconds(self) -> float:
        """Convert uptime_ms to seconds"""
        return self.uptime_ms / 1000.0

    def get_datetime(self, base_date: Optional[datetime] = None) -> datetime:
        """Convert timestamp to datetime object"""
        if base_date is None:
            base_date = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)

        time_parts = self.timestamp.split(':')
        hours = int(time_parts[0])
        minutes = int(time_parts[1])
        seconds_parts = time_parts[2].split('.')
        seconds = int(seconds_parts[0])
        milliseconds = int(seconds_parts[1])

        return base_date.replace(
            hour=hours,
            minute=minutes,
            second=seconds,
            microsecond=milliseconds * 1000
        )


class PeriodicityParser:
    """Parser for extracting message events from ESP32 LoRa logs"""

    # Regex patterns
    PACKET_PATTERN = re.compile(
        r'(\d{2}:\d{2}:\d{2}\.\d{3})\s+>\s+\w\s+\((\d+)\)\s+LoRaMesher:\s+Packet\s+(send|received)\s+--\s+'
        r'Size:\s+(\d+)\s+Src:\s+([0-9A-Fa-f]+)\s+Dst:\s+([0-9A-Fa-f]+)\s+'
        r'Id:\s+(\d+)\s+Type:\s+(\d+)\s+Via:\s+([0-9A-Fa-f]+)\s+Seq_Id:\s+(\d+)\s+Num:\s+(\d+)'
    )

    ADDRESS_PATTERN = re.compile(r'Local address: ([0-9A-Fa-f]+)')

    def __init__(self):
        self.device_addresses: Dict[str, str] = {}  # com_port -> device_id

    def parse_file(self, file_path: str) -> List[MessageEvent]:
        """
        Parse a single .ans log file and extract all message events

        Args:
            file_path: Path to the .ans log file

        Returns:
            List of MessageEvent objects
        """
        messages = []
        com_port = self._extract_com_port(file_path)
        device_id = None

        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    # Look for device address
                    if device_id is None:
                        addr_match = self.ADDRESS_PATTERN.search(line)
                        if addr_match:
                            device_id = addr_match.group(1).upper()
                            self.device_addresses[com_port] = device_id

                    # Look for packet send/receive events
                    packet_match = self.PACKET_PATTERN.search(line)
                    if packet_match:
                        timestamp = packet_match.group(1)
                        uptime_ms = int(packet_match.group(2))
                        direction = packet_match.group(3)
                        size = int(packet_match.group(4))
                        src_addr = packet_match.group(5).upper()
                        dst_addr = packet_match.group(6).upper()
                        packet_id = int(packet_match.group(7))
                        packet_type = int(packet_match.group(8))
                        via = packet_match.group(9).upper()
                        seq_id = int(packet_match.group(10))
                        num = int(packet_match.group(11))

                        # Decode packet type bitmask to human-readable name
                        packet_type_name = decode_packet_type(packet_type)

                        # Use device_id if found, otherwise use src_addr for sent packets
                        if device_id is None and direction == "send":
                            device_id = src_addr
                            self.device_addresses[com_port] = device_id

                        event = MessageEvent(
                            timestamp=timestamp,
                            uptime_ms=uptime_ms,
                            device_id=device_id or src_addr,
                            com_port=com_port,
                            direction=direction,
                            src_addr=src_addr,
                            dst_addr=dst_addr,
                            packet_type=packet_type,
                            packet_type_name=packet_type_name,
                            size=size,
                            packet_id=packet_id,
                            via=via,
                            seq_id=seq_id,
                            num=num
                        )
                        messages.append(event)

        except Exception as e:
            print(f"Error parsing file {file_path}: {e}")

        return messages

    def parse_multiple_files(self, file_paths: List[str]) -> List[MessageEvent]:
        """
        Parse multiple .ans log files and combine events

        Args:
            file_paths: List of paths to .ans log files

        Returns:
            Combined list of MessageEvent objects, sorted by timestamp
        """
        all_messages = []

        for file_path in file_paths:
            messages = self.parse_file(file_path)
            all_messages.extend(messages)

        # Sort by uptime_ms within each device, then by device_id
        all_messages.sort(key=lambda x: (x.device_id, x.uptime_ms))

        return all_messages

    def _extract_com_port(self, file_path: str) -> str:
        """Extract COM port from file path"""
        import os
        filename = os.path.basename(file_path)

        # Look for patterns like "monitor_COM10.ans" or "COM10.ans"
        com_match = re.search(r'COM\d+', filename)
        if com_match:
            return com_match.group(0)

        return "UNKNOWN"

    def get_device_mapping(self) -> Dict[str, str]:
        """Get mapping of COM ports to device IDs"""
        return self.device_addresses.copy()

    def filter_by_type(self, messages: List[MessageEvent], packet_types: List[int]) -> List[MessageEvent]:
        """Filter messages by packet type"""
        return [msg for msg in messages if msg.packet_type in packet_types]

    def filter_by_device(self, messages: List[MessageEvent], device_ids: List[str]) -> List[MessageEvent]:
        """Filter messages by device ID"""
        return [msg for msg in messages if msg.device_id in device_ids]

    def filter_by_direction(self, messages: List[MessageEvent], direction: str) -> List[MessageEvent]:
        """Filter messages by direction (send/received)"""
        return [msg for msg in messages if msg.direction == direction]

    def group_by_device(self, messages: List[MessageEvent]) -> Dict[str, List[MessageEvent]]:
        """Group messages by device ID"""
        grouped = {}
        for msg in messages:
            if msg.device_id not in grouped:
                grouped[msg.device_id] = []
            grouped[msg.device_id].append(msg)
        return grouped

    def group_by_type(self, messages: List[MessageEvent]) -> Dict[str, List[MessageEvent]]:
        """Group messages by packet type"""
        grouped = {}
        for msg in messages:
            type_name = msg.packet_type_name
            if type_name not in grouped:
                grouped[type_name] = []
            grouped[type_name].append(msg)
        return grouped


if __name__ == "__main__":
    # Test the parser
    import sys
    import glob

    if len(sys.argv) > 1:
        pattern = sys.argv[1]
    else:
        pattern = "etx_validation_tests5/first/Monitoring/monitor_*.ans"

    files = glob.glob(pattern)
    print(f"Found {len(files)} files to parse")

    parser = PeriodicityParser()
    messages = parser.parse_multiple_files(files)

    print(f"\nParsed {len(messages)} message events")
    print(f"\nDevice mapping: {parser.get_device_mapping()}")

    # Show first few messages
    print("\nFirst 10 messages:")
    for i, msg in enumerate(messages[:10]):
        print(f"{i+1}. [{msg.timestamp}] {msg.device_id} {msg.direction} "
              f"{msg.packet_type_name} to {msg.dst_addr} (size: {msg.size})")

    # Group by type
    by_type = parser.group_by_type(messages)
    print("\nMessages by type:")
    for type_name, msgs in sorted(by_type.items()):
        print(f"  {type_name}: {len(msgs)} messages")
