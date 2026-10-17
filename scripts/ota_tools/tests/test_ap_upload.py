import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ap_upload as au  # noqa: E402


class FakeBridge:
    """Serial port of a bridge that answers like extras/bench_ap_bridge."""

    def __init__(self, response=b"HTTP/1.1 200 OK\r\n\r\nOK: 0.1.2 written", join_ok=True):
        self.out = []
        self.inbox = bytearray()
        self.received = bytearray()
        self.blocks = []
        self.expect_body = 0
        self.length = 0
        self.response = response
        self.join_ok = join_ok
        self.lines_written = []
        self.pings = 0
        self.lost_pings = 0

    def write(self, data):
        if self.expect_body:
            assert len(data) <= au.BLOCK_SIZE
            self.blocks.append(len(data))
            self.received += data
            self.expect_body -= len(data)
            self.out.append(f"ACK {len(self.received)}\n".encode())
            if self.expect_body == 0:
                if len(self.received) < self.length:
                    self.out.append(b"CUT\n")
                else:
                    self.out += [b"RESPONSE\n", self.response + b"\n", b"END\n"]
            return len(data)
        self.inbox += data
        while b"\n" in self.inbox:
            line, _, rest = bytes(self.inbox).partition(b"\n")
            self.inbox = bytearray(rest)
            self.command(line.decode())
        return len(data)

    def command(self, line):
        self.lines_written.append(line)
        if line == "PING":
            self.pings += 1
            if self.pings > self.lost_pings:
                self.out.append(b"PONG\n")
        elif line.startswith("JOIN "):
            self.out.append(b"OK JOINED 192.168.4.2 RSSI -40\n" if self.join_ok else b"ERR join\n")
        elif line.startswith("GET "):
            self.out += [b"RESPONSE\n", b'HTTP/1.1 200 OK\r\n\r\n{"version":"0.1.1"}\n', b"END\n"]
        elif line.startswith("POST "):
            _, path, length, send = line.split()
            self.length, self.expect_body = int(length), int(send)
            self.out.append(b"READY\n")

    def readline(self):
        return self.out.pop(0) if self.out else b""


def test_sync_retries_until_the_bridge_answers():
    port = FakeBridge()
    port.lost_pings = 2
    au.Bridge(port, timeout=0.05).sync(attempts=5)
    assert port.pings == 3


def test_sync_gives_up():
    port = FakeBridge()
    port.lost_pings = 10
    with pytest.raises(au.BridgeError):
        au.Bridge(port, timeout=0.05).sync(attempts=3)


def test_join_returns_the_bridge_address():
    bridge = au.Bridge(FakeBridge())
    assert bridge.join("LM-7680", "secret") == "OK JOINED 192.168.4.2 RSSI -40"


def test_join_failure_raises():
    with pytest.raises(au.BridgeError):
        au.Bridge(FakeBridge(join_ok=False), timeout=0.2).join("LM-7680", "secret")


def test_status_returns_the_body():
    assert au.Bridge(FakeBridge()).get("/status").endswith('{"version":"0.1.1"}')


def test_upload_sends_all_bytes_in_blocks_and_returns_the_reply():
    port = FakeBridge()
    data = bytes(range(256)) * 50
    reply = au.Bridge(port).post("/update", data)
    assert bytes(port.received) == data
    assert max(port.blocks) == au.BLOCK_SIZE and sum(port.blocks) == len(data)
    assert port.lines_written[-1] == f"POST /update {len(data)} {len(data)}"
    assert reply.endswith("OK: 0.1.2 written")


def test_cut_upload_announces_the_full_length_and_sends_part():
    port = FakeBridge()
    data = bytes(10000)
    assert au.Bridge(port).post("/update", data, send=5000) == "CUT"
    assert len(port.received) == 5000
    assert port.lines_written[-1] == "POST /update 10000 5000"
