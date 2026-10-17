import json
import os
import stat
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import sign_manifest as sm  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402

FIELDS = dict(key_id=1, version=0x00010300, image_size=1182800, board_env="tbeam",
              project="LoRaChat", version_string="0.1.3+gabc1234",
              image_sha256=bytes(range(32)), partition_prefix=bytes(range(0xA0, 0xA8)),
              flags=sm.FLAG_SKIP_MESH_CHECK)


def test_body_layout_matches_the_firmware():
    body = sm.encode_body(**FIELDS)
    assert len(body) == 128
    assert body[:4] == b"LMOT" and body[4] == 1 and body[5] == 1
    assert struct.unpack_from("<HIII", body, 6) == (sm.FLAG_SKIP_MESH_CHECK, 1, 0x00010300,
                                                    1182800)
    assert body[24:29] == b"tbeam" and body[40:48] == b"LoRaChat"
    assert body[56:70] == b"0.1.3+gabc1234"
    assert body[88:120] == bytes(range(32)) and body[120:128] == bytes(range(0xA0, 0xA8))


def test_body_round_trips():
    fields = sm.decode_body(sm.encode_body(**FIELDS))
    assert fields["version_string"] == "0.1.3+gabc1234"
    assert fields["image_sha256"] == bytes(range(32)).hex()


@pytest.mark.parametrize("name,value", [("board_env", "x" * 16), ("project", "p" * 16),
                                        ("version_string", "1" * 32)])
def test_rejects_strings_that_do_not_fit(name, value):
    with pytest.raises(sm.ManifestError):
        sm.encode_body(**dict(FIELDS, **{name: value}))


def test_signature_verifies_and_detects_tampering():
    key = ec.generate_private_key(ec.SECP256R1())
    body = sm.encode_body(**FIELDS)
    signed = body + sm.sign_body(body, key)
    public = sm.public_key_bytes(key)
    assert len(signed) == 192 and len(public) == 65 and public[0] == 0x04
    assert sm.verify(signed, public)
    tampered = bytearray(signed)
    tampered[100] ^= 1
    assert not sm.verify(bytes(tampered), public)
    other = sm.public_key_bytes(ec.generate_private_key(ec.SECP256R1()))
    assert not sm.verify(signed, other)


def make_release(tmp_path, image=b"\xe9" + bytes(100)):
    import hashlib
    image = image + hashlib.sha256(image).digest()
    release = tmp_path / "rel"
    release.mkdir()
    (release / "firmware.bin").write_bytes(image)
    (release / "info.json").write_text(json.dumps({
        "env": "tbeam", "project": "LoRaChat", "version": "0.1.3+gabc1234",
        "version_u32": 0x00010300, "image_size": len(image), "image_sha256": image[-32:].hex(),
        "partitions_sha256": "a0a1a2a3a4a5a6a7" + "00" * 24}))
    return release, image


def test_sign_release_writes_manifest_and_bundle(tmp_path):
    release, image = make_release(tmp_path)
    key = ec.generate_private_key(ec.SECP256R1())
    signed = sm.sign_release(release, key, 0x100, sm.FLAG_ALLOW_DOWNGRADE)
    assert (release / "manifest.bin").read_bytes() == signed
    assert (release / "bundle.bin").read_bytes() == signed + image
    fields = sm.decode_body(signed[:128])
    assert fields["key_id"] == 0x100 and fields["flags"] == sm.FLAG_ALLOW_DOWNGRADE
    assert fields["image_sha256"] == image[-32:].hex()
    assert fields["partition_prefix"] == "a0a1a2a3a4a5a6a7"
    assert sm.verify(signed, sm.public_key_bytes(key))


def test_sign_release_refuses_an_image_that_does_not_match_info(tmp_path):
    release, image = make_release(tmp_path)
    (release / "firmware.bin").write_bytes(image[:-1] + bytes([image[-1] ^ 1]))
    with pytest.raises(sm.ManifestError):
        sm.sign_release(release, ec.generate_private_key(ec.SECP256R1()), 0x100)


def test_keygen_writes_a_private_file_and_never_overwrites(tmp_path):
    out = tmp_path / "key.pem"
    public = sm.keygen(out)
    assert len(public) == 65
    assert stat.S_IMODE(out.stat().st_mode) == 0o600
    assert sm.public_key_bytes(sm.load_key(out)) == public
    with pytest.raises(sm.ManifestError):
        sm.keygen(out)


def test_committed_vectors_match_the_test_key():
    vectors = json.loads((sm.VECTORS_DIR / "manifest_vectors.json").read_text())
    public = sm.public_key_bytes(sm.load_key(sm.TEST_KEY))
    assert vectors["public_key"] == public.hex()
    for name, vector in vectors["vectors"].items():
        assert sm.verify(bytes.fromhex(vector["manifest"]), public) == vector["valid"], name


def c_array_bytes(header: str, name: str) -> bytes:
    import re
    match = re.search(name + r"\[\d+\] = \{(.*?)\};", header, re.S)
    assert match, name
    return bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", match.group(1)))


def test_firmware_test_key_header_matches_the_vectors():
    header = sm.FIRMWARE_TEST_KEY_HEADER.read_text()
    vectors = json.loads((sm.VECTORS_DIR / "manifest_vectors.json").read_text())
    public = sm.public_key_bytes(sm.load_key(sm.TEST_KEY))
    assert c_array_bytes(header, "OTA_TEST_PUBLIC_KEY") == public
    manifest = c_array_bytes(header, "OTA_SELF_TEST_MANIFEST")
    assert manifest.hex() == vectors["vectors"]["valid"]["manifest"]
    assert sm.verify(manifest, public)


def test_show_prints_fields_and_checks_the_signature(tmp_path, capsys):
    release, _ = make_release(tmp_path)
    key_path = tmp_path / "key.pem"
    sm.keygen(key_path)
    sm.sign_release(release, sm.load_key(key_path), 0x100)
    manifest = release / "manifest.bin"

    assert sm.main(["show", str(manifest), "--key", str(key_path)]) == 0
    out = capsys.readouterr().out
    assert "version_string: 0.1.3+gabc1234" in out and "signature: valid" in out

    other = tmp_path / "other.pem"
    sm.keygen(other)
    assert sm.main(["show", str(manifest), "--key", str(other)]) == 1
    assert "signature: INVALID" in capsys.readouterr().out
