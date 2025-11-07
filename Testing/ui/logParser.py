"""
Log Parser for LoRaMesher Routing Table Serial Logs

Parses .ans (ANSI) serial monitor log files to extract routing table snapshots.

Log Format Example:
    18:29:54.124 > I (130436) LoRaMesher: Current routing table:
    18:29:54.130 > I (130440) LoRaMesher: 0 - 4E58 via 4E58 ETX=2.0 (R:1.0+F:1.0) asym:1.00 hops:1 Role:1
    18:29:54.135 > I (130448) LoRaMesher:     └─ Direct: hellos=2/2 (100.0%) SNR=9
"""

import re
from dataclasses import dataclass
from typing import List, Optional, Tuple
from pathlib import Path


@dataclass
class LinkQuality:
    """Link quality metrics for direct connections"""
    hellos_received: int
    hellos_expected: int
    hello_rate: float
    snr: int


@dataclass
class RouteEntry:
    """Single routing table entry"""
    index: int
    address: str          # Hex address (e.g., "4E58")
    via: str              # Next hop address
    total_etx: float      # Total ETX value
    reverse_etx: float    # Reverse ETX (neighbor → us)
    forward_etx: float    # Forward ETX (us → neighbor)
    asymmetry: float      # Asymmetry ratio
    hops: int             # Hop count
    role: int             # 0=regular, 1=gateway
    link_quality: Optional[LinkQuality] = None  # Only for direct links
    is_timed_out: bool = False  # Flag indicating if this route has timed out


@dataclass
class RoutingTableSnapshot:
    """Complete routing table snapshot for one device at one point in time"""
    device_id: str        # Hex address of this device
    com_port: str         # COM port (e.g., "COM10")
    timestamp: str        # Time string (e.g., "18:29:54.124")
    uptime_ms: int        # Device uptime in milliseconds
    routes: List[RouteEntry]


@dataclass
class RouteTimeoutEvent:
    """Route timeout event"""
    device_id: str           # Hex address of device that detected timeout
    com_port: str            # COM port (e.g., "COM10")
    timestamp: str           # Time string (e.g., "18:47:50.302")
    uptime_ms: int           # Device uptime in milliseconds
    timed_out_address: str   # Address that timed out
    via: str                 # Via address (next hop)


class LogParser:
    """Parser for LoRaMesher serial monitor logs"""

    # Regex patterns
    TIMESTAMP_PATTERN = r'^(\d{2}:\d{2}:\d{2}\.\d{3})'
    ROUTING_TABLE_START = r'Current routing table:'
    ROUTE_ENTRY_PATTERN = r'(\d+) - ([0-9A-F]+) via ([0-9A-F]+) ETX=([\d.]+) \(R:([\d.]+)\+F:([\d.]+)\) asym:([\d.]+) hops:(\d+) Role:(\d+)'
    LINK_QUALITY_PATTERN = r'└─ Direct: hellos=(\d+)/(\d+) \(([\d.]+)%\) SNR[=:]?(-?\d+)'
    ROUTE_TIMEOUT_PATTERN = r'Route timeout ([0-9A-F]+) via ([0-9A-F]+)'

    def __init__(self):
        self.route_entry_re = re.compile(self.ROUTE_ENTRY_PATTERN)
        self.link_quality_re = re.compile(self.LINK_QUALITY_PATTERN)
        self.timestamp_re = re.compile(self.TIMESTAMP_PATTERN)
        self.timeout_re = re.compile(self.ROUTE_TIMEOUT_PATTERN)

    def parse_log_file(self, file_path: str) -> Tuple[List[RoutingTableSnapshot], List[RouteTimeoutEvent]]:
        """
        Parse entire log file and extract all routing table snapshots and timeout events

        Args:
            file_path: Path to .ans log file

        Returns:
            Tuple of (List of RoutingTableSnapshot objects, List of RouteTimeoutEvent objects)
        """
        file_path_obj = Path(file_path)
        if not file_path_obj.exists():
            raise FileNotFoundError(f"Log file not found: {file_path}")

        # Extract COM port from filename (e.g., "monitor_COM10.ans" -> "COM10")
        com_port = self._extract_com_port(file_path_obj.name)

        # Read all lines
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()

        # Extract device address from log
        device_address = self._extract_device_address(lines)

        # Parse routing table sections and timeout events
        snapshots, timeouts = self._parse_lines(lines, com_port, device_address)

        return snapshots, timeouts

    def parse_log_lines(self, lines: List[str], file_path: str, device_address: str = "UNKNOWN") -> Tuple[List[RoutingTableSnapshot], List[RouteTimeoutEvent]]:
        """
        Parse a list of log lines (for incremental parsing)

        Args:
            lines: List of log lines
            file_path: Path to source file (for COM port extraction)
            device_address: Device address (if known)

        Returns:
            Tuple of (List of RoutingTableSnapshot objects, List of RouteTimeoutEvent objects)
        """
        file_path_obj = Path(file_path)
        com_port = self._extract_com_port(file_path_obj.name)

        return self._parse_lines(lines, com_port, device_address)

    def _parse_lines(self, lines: List[str], com_port: str, device_address: str = "UNKNOWN") -> Tuple[List[RoutingTableSnapshot], List[RouteTimeoutEvent]]:
        """
        Internal method to parse lines and extract routing table snapshots and timeout events
        """
        snapshots = []
        timeouts = []
        i = 0

        while i < len(lines):
            line = lines[i]

            # Check if this line starts a routing table section
            if self.ROUTING_TABLE_START in line:
                # Extract timestamp and uptime
                timestamp, uptime_ms = self._extract_timestamp_and_uptime(line)

                # Parse routing entries following this line
                routes, lines_consumed = self._parse_routing_entries(lines[i+1:])

                # Only create snapshot if we have routes
                if routes:
                    snapshot = RoutingTableSnapshot(
                        device_id=device_address,
                        com_port=com_port,
                        timestamp=timestamp,
                        uptime_ms=uptime_ms,
                        routes=routes
                    )
                    snapshots.append(snapshot)

                i += lines_consumed + 1  # Skip parsed lines

            # Check for route timeout
            elif self.timeout_re.search(line):
                timeout_match = self.timeout_re.search(line)
                if timeout_match:
                    timed_out_address, via = timeout_match.groups()
                    timestamp, uptime_ms = self._extract_timestamp_and_uptime(line)

                    timeout_event = RouteTimeoutEvent(
                        device_id=device_address,
                        com_port=com_port,
                        timestamp=timestamp,
                        uptime_ms=uptime_ms,
                        timed_out_address=timed_out_address,
                        via=via
                    )
                    timeouts.append(timeout_event)

            i += 1

        return snapshots, timeouts

    def _parse_routing_entries(self, lines: List[str]) -> Tuple[List[RouteEntry], int]:
        """
        Parse consecutive routing table entry lines

        Returns:
            (list of RouteEntry objects, number of lines consumed from input)
        """
        routes = []
        lines_checked = 0  # Total lines looked at (including blank lines)
        found_any_entry = False  # Track if we've found at least one routing entry

        for line in lines:
            lines_checked += 1

            # Skip blank lines
            if not line.strip():
                continue

            # Try to match route entry
            route_match = self.route_entry_re.search(line)
            if route_match:
                found_any_entry = True

                # Extract route data
                index, address, via, total_etx, reverse_etx, forward_etx, asymmetry, hops, role = route_match.groups()

                route = RouteEntry(
                    index=int(index),
                    address=address,
                    via=via,
                    total_etx=float(total_etx),
                    reverse_etx=float(reverse_etx),
                    forward_etx=float(forward_etx),
                    asymmetry=float(asymmetry),
                    hops=int(hops),
                    role=int(role),
                    link_quality=None
                )

                routes.append(route)
                continue

            # Try to match link quality line (follows a route entry)
            link_match = self.link_quality_re.search(line)
            if link_match and routes:
                # This link quality belongs to the last route
                hellos_received, hellos_expected, hello_rate, snr = link_match.groups()

                link_quality = LinkQuality(
                    hellos_received=int(hellos_received),
                    hellos_expected=int(hellos_expected),
                    hello_rate=float(hello_rate),
                    snr=int(snr)
                )

                # Attach to last route
                routes[-1].link_quality = link_quality
                continue

            # If line doesn't match route entry or link quality, routing table section is done
            if found_any_entry:
                # We've finished parsing this routing table section
                # Don't count this line as it's not part of the routing table
                lines_checked -= 1
                break

        return routes, lines_checked

    def _extract_timestamp_and_uptime(self, line: str) -> Tuple[str, int]:
        """
        Extract timestamp and uptime from log line

        Example: "18:29:54.124 > I (130436) LoRaMesher: Current routing table:"
        Returns: ("18:29:54.124", 130436)
        """
        # Extract timestamp (HH:MM:SS.mmm)
        timestamp_match = self.timestamp_re.search(line)
        timestamp = timestamp_match.group(1) if timestamp_match else "00:00:00.000"

        # Extract uptime in milliseconds (number in parentheses)
        uptime_match = re.search(r'\((\d+)\)', line)
        uptime_ms = int(uptime_match.group(1)) if uptime_match else 0

        return timestamp, uptime_ms

    def _extract_com_port(self, filename: str) -> str:
        """
        Extract COM port from filename

        Example: "monitor_COM10.ans" -> "COM10"
        """
        match = re.search(r'(COM\d+)', filename, re.IGNORECASE)
        if match:
            return match.group(1).upper()
        return "UNKNOWN"

    def _extract_device_address(self, lines: List[str]) -> str:
        """
        Extract device address from log file

        Looks for line like: "Local LoRa address (from WiFi MAC): 0x571C"

        Args:
            lines: Log file lines

        Returns:
            Device address in hex (e.g., "571C") or "UNKNOWN"
        """
        # Search first 1000 lines for device address (it should be near the start)
        # Format: "Local LoRa address (from WiFi MAC): 571C" or "... 0x571C"
        for line in lines[:1000]:
            match = re.search(r'Local LoRa address.*?:\s*(?:0x)?([0-9A-Fa-f]{4})', line)
            if match:
                return match.group(1).upper()

        return "UNKNOWN"


# Convenience functions

def parse_log_file(file_path: str) -> Tuple[List[RoutingTableSnapshot], List[RouteTimeoutEvent]]:
    """
    Convenience function to parse a log file

    Args:
        file_path: Path to .ans log file

    Returns:
        Tuple of (List of RoutingTableSnapshot objects, List of RouteTimeoutEvent objects)
    """
    parser = LogParser()
    return parser.parse_log_file(file_path)


def parse_log_lines(lines: List[str], file_path: str) -> Tuple[List[RoutingTableSnapshot], List[RouteTimeoutEvent]]:
    """
    Convenience function to parse log lines

    Args:
        lines: List of log lines
        file_path: Path to source file

    Returns:
        Tuple of (List of RoutingTableSnapshot objects, List of RouteTimeoutEvent objects)
    """
    parser = LogParser()
    return parser.parse_log_lines(lines, file_path)


if __name__ == "__main__":
    # Test the parser
    import sys

    if len(sys.argv) < 2:
        print("Usage: python logParser.py <log_file.ans>")
        sys.exit(1)

    log_file = sys.argv[1]

    print(f"Parsing log file: {log_file}")
    print("=" * 70)

    snapshots, timeouts = parse_log_file(log_file)

    print(f"\nFound {len(snapshots)} routing table snapshots")
    print(f"Found {len(timeouts)} route timeout events\n")

    for i, snapshot in enumerate(snapshots[:5]):  # Show first 5
        print(f"Snapshot {i+1}:")
        print(f"  COM Port: {snapshot.com_port}")
        print(f"  Device ID: {snapshot.device_id}")
        print(f"  Timestamp: {snapshot.timestamp}")
        print(f"  Uptime: {snapshot.uptime_ms}ms ({snapshot.uptime_ms/1000:.1f}s)")
        print(f"  Routes: {len(snapshot.routes)}")

        for route in snapshot.routes:
            etx_str = f"ETX={route.total_etx:.1f} (R:{route.reverse_etx:.1f}+F:{route.forward_etx:.1f})"
            role_str = "[Gateway]" if route.role == 1 else ""
            print(f"    {route.index}. {route.address} via {route.via} {etx_str} hops:{route.hops} {role_str}")

            if route.link_quality:
                lq = route.link_quality
                print(f"       └─ hellos={lq.hellos_received}/{lq.hellos_expected} ({lq.hello_rate:.1f}%) SNR={lq.snr}")

        print()

    if len(snapshots) > 5:
        print(f"... and {len(snapshots) - 5} more snapshots")

    # Show timeout events
    if timeouts:
        print(f"\n\nTimeout Events:")
        for i, timeout in enumerate(timeouts[:5]):  # Show first 5
            print(f"  {i+1}. {timeout.timestamp} ({timeout.uptime_ms/1000:.1f}s): {timeout.com_port} - Route {timeout.timed_out_address} via {timeout.via}")
        if len(timeouts) > 5:
            print(f"  ... and {len(timeouts) - 5} more timeout events")
