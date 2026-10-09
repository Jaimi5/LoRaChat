import hashlib
import hmac
import os
import re
import stat
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import ap_pass as ap  # noqa: E402

KEY = bytes(range(32))


def test_password_is_the_truncated_hmac_in_hex():
    expected = hmac.new(KEY, b"LMAP1" + struct.pack("<H", 0x1A2B), hashlib.sha256).digest()[:8]
    password = ap.ap_password(KEY, 0x1A2B)
    assert password == expected.hex()
    assert re.fullmatch(r"[0-9a-f]{16}", password)


def test_password_depends_on_node_and_key():
    assert ap.ap_password(KEY, 0x1A2B) != ap.ap_password(KEY, 0x1A2C)
    assert ap.ap_password(KEY, 0x1A2B) != ap.ap_password(bytes(32), 0x1A2B)


def test_fingerprint_is_the_sha256_prefix():
    assert ap.key_fingerprint(KEY) == hashlib.sha256(KEY).digest()[:4].hex()


def test_ssid_and_qr():
    assert ap.ap_ssid(0x1A2B) == "LM-1A2B"
    assert ap.ap_ssid(0x00C) == "LM-000C"
    password = ap.ap_password(KEY, 0x1A2B)
    assert ap.wifi_qr(KEY, 0x1A2B) == f"WIFI:T:WPA;S:LM-1A2B;P:{password};;"


@pytest.mark.parametrize("text", ["1a2b", "0x1A2B", "1A2B"])
def test_parses_node_ids(text):
    assert ap.parse_node(text) == 0x1A2B


@pytest.mark.parametrize("text", ["", "12345", "zz", "0x10000"])
def test_rejects_bad_node_ids(text):
    with pytest.raises(ap.KeyFileError):
        ap.parse_node(text)


def test_keygen_writes_a_private_hex_file_and_never_overwrites(tmp_path):
    out = tmp_path / "deploy.key"
    key = ap.keygen(out)
    assert len(key) == 32
    assert stat.S_IMODE(out.stat().st_mode) == 0o600
    assert ap.load_key(out) == key
    with pytest.raises(ap.KeyFileError):
        ap.keygen(out)


@pytest.mark.parametrize("content", ["00" * 31, "zz" * 32, "00" * 33])
def test_load_key_rejects_bad_files(tmp_path, content):
    path = tmp_path / "bad.key"
    path.write_text(content)
    with pytest.raises(ap.KeyFileError):
        ap.load_key(path)


def test_show_prints_ssid_password_and_qr(tmp_path, capsys):
    path = tmp_path / "deploy.key"
    path.write_text(KEY.hex() + "\n")
    assert ap.main(["show", "--key-file", str(path), "--node", "1A2B"]) == 0
    out = capsys.readouterr().out
    assert "LM-1A2B" in out and ap.ap_password(KEY, 0x1A2B) in out and "WIFI:T:WPA" in out
    assert ap.key_fingerprint(KEY) in out


def test_committed_vectors_match():
    header = (ap.VECTORS_DIR / "apPassVectors.h").read_text()
    assert ap.vectors_header() == header
