import hashlib
import hmac
import json
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import lmcmd  # noqa: E402

KEY = bytes(range(32))


def test_auth_message_layout():
    assert lmcmd.auth_message(0x1A2B, 0x01020304, "/reboot") == (
        b"LMC1" + b"\x2b\x1a" + b"\x04\x03\x02\x01" + b"/reboot")


def test_tag_is_the_truncated_hmac():
    expected = hmac.new(KEY, lmcmd.auth_message(0x7680, 5, "/reboot"), hashlib.sha256).digest()
    assert lmcmd.command_tag(KEY, 0x7680, 5, "/reboot") == expected[:8]


def test_signed_line_for_the_serial_console():
    line = lmcmd.signed_line(KEY, 0x7680, 0x65000000, "/maint.open 600 ap")
    tag = lmcmd.command_tag(KEY, 0x7680, 0x65000000, "/maint.open 600 ap").hex()
    assert line == f"@7680 /maint.open 600 ap #65000000.{tag}"


def test_seal_is_its_own_inverse_and_depends_on_dst_and_counter():
    data = b"BenchNet\0password123"
    sealed = lmcmd.seal_args(KEY, 0x7680, 9, data)
    assert len(sealed) == len(data) and sealed != data
    assert lmcmd.seal_args(KEY, 0x7680, 9, sealed) == data
    assert lmcmd.seal_args(KEY, 0x7680, 10, data) != sealed
    assert lmcmd.seal_args(KEY, 0x7681, 9, data) != sealed


def test_seal_keystream_definition():
    seal_key = hmac.new(KEY, b"LMW1", hashlib.sha256).digest()
    block0 = hmac.new(seal_key, struct.pack("<HIB", 0x7680, 9, 0), hashlib.sha256).digest()
    block1 = hmac.new(seal_key, struct.pack("<HIB", 0x7680, 9, 1), hashlib.sha256).digest()
    data = bytes(40)
    assert lmcmd.seal_args(KEY, 0x7680, 9, data) == (block0 + block1)[:40]


def test_wifi_line_round_trips():
    line = lmcmd.wifi_line(KEY, 0x7680, 9, "BenchNet", "password123")
    name, hex_args = line.split(" ")
    assert name == "/maint.wifi"
    plain = lmcmd.seal_args(KEY, 0x7680, 9, bytes.fromhex(hex_args))
    assert plain == b"BenchNet\0password123"


@pytest.mark.parametrize("ssid,password", [("", "password123"), ("x" * 33, "password123"),
                                           ("ssid", "short"), ("ssid", "p" * 64)])
def test_wifi_line_refuses_invalid_credentials(ssid, password):
    with pytest.raises(lmcmd.CommandError):
        lmcmd.wifi_line(KEY, 0x7680, 9, ssid, password)


def test_wifi_line_accepts_an_open_network():
    lmcmd.wifi_line(KEY, 0x7680, 9, "OpenNet", "")


def test_counter_grows_and_follows_the_clock(tmp_path):
    state = tmp_path / "counters.json"
    first = lmcmd.next_counter(state, KEY, now=1000)
    second = lmcmd.next_counter(state, KEY, now=1000)
    third = lmcmd.next_counter(state, KEY, now=5000)
    assert (first, second, third) == (1000, 1001, 5000)
    stored = json.loads(state.read_text())
    assert stored == {lmcmd.key_id(KEY): 5000}


def test_sign_command_prints_the_line(tmp_path, capsys):
    key_file = tmp_path / "k.key"
    key_file.write_text(KEY.hex())
    state = tmp_path / "c.json"
    assert lmcmd.main(["sign", "--key-file", str(key_file), "--node", "7680",
                       "--state", str(state), "/reboot"]) == 0
    out = capsys.readouterr().out.strip()
    assert out.startswith("@7680 /reboot #")


def test_sign_for_mqtt_prints_the_payload(tmp_path, capsys):
    key_file = tmp_path / "k.key"
    key_file.write_text(KEY.hex())
    assert lmcmd.main(["sign", "--key-file", str(key_file), "--node", "7680", "--state",
                       str(tmp_path / "c.json"), "--mqtt", "12", "/reboot"]) == 0
    out = capsys.readouterr().out.strip()
    assert out.startswith("7680 12 /reboot #")
    counter = int(out.split("#")[1].split(".")[0], 16)
    assert out.endswith(lmcmd.command_tag(KEY, 0x7680, counter, "/reboot").hex())


def test_mqtt_payload_format():
    assert lmcmd.mqtt_payload(KEY, 0x7680, 5, 12, "/reboot") == (
        "7680 12 /reboot" + lmcmd.format_signature(5, lmcmd.command_tag(KEY, 0x7680, 5, "/reboot")))


def test_committed_vectors_match():
    assert (lmcmd.VECTORS_DIR / "cmdVectors.h").read_text() == lmcmd.vectors_header()
