#!/usr/bin/env python3
"""Creates and signs OTA manifests (same layout as lib/OtaCore/src/otaManifest.cpp).

Commands:
  keygen --out key.pem                 new ECDSA P-256 key; prints the public key for otaKeys.cpp
  sign RELEASE_DIR --key key.pem --key-id N [--allow-downgrade] [--skip-mesh-check]
                                       writes manifest.bin (192 B) and bundle.bin (manifest + image)
  show manifest.bin [--key key.pem]    prints the fields; with a key also checks the signature
  vectors                              regenerates the test vectors in test/vectors/ and the
                                       firmware's copy of the test key (src/ota/otaTestKey.h)

A manifest is a 128-byte body followed by an ECDSA P-256 signature (raw r || s) over the
SHA-256 of the body. Key id 1 is the test key in test/vectors/test_key.pem; production keys
use ids from 0x100 and never enter the repository.
"""
import argparse
import json
import os
import struct
import sys
from pathlib import Path
from typing import Dict, Optional

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import (decode_dss_signature,
                                                            encode_dss_signature)

ROOT = Path(__file__).resolve().parents[2]
VECTORS_DIR = ROOT / "test" / "vectors"
FIRMWARE_TEST_KEY_HEADER = ROOT / "src" / "ota" / "otaTestKey.h"
TEST_KEY = VECTORS_DIR / "test_key.pem"
TEST_KEY_ID = 0x00000001

MAGIC = b"LMOT"
FORMAT_VERSION = 1
IMAGE_TYPE_FULL_APP = 1
FLAG_ALLOW_DOWNGRADE = 1 << 0
FLAG_SKIP_MESH_CHECK = 1 << 1
BODY_SIZE = 128
SIGNATURE_SIZE = 64
BOARD_ENV_SIZE = 16
PROJECT_SIZE = 16
VERSION_STRING_SIZE = 32


class ManifestError(Exception):
    pass


def _field(text: str, size: int, name: str) -> bytes:
    raw = text.encode("ascii")
    if len(raw) >= size:
        raise ManifestError(f"{name} {text!r} does not fit {size - 1} characters")
    return raw.ljust(size, b"\0")


def encode_body(*, key_id: int, version: int, image_size: int, board_env: str, project: str,
                version_string: str, image_sha256: bytes, partition_prefix: bytes,
                flags: int = 0, image_type: int = IMAGE_TYPE_FULL_APP) -> bytes:
    """@return the 128-byte manifest body."""
    if len(image_sha256) != 32 or len(partition_prefix) != 8:
        raise ManifestError("image SHA-256 must be 32 bytes and the partition prefix 8 bytes")
    body = (MAGIC + struct.pack("<BBHIIII", FORMAT_VERSION, image_type, flags, key_id, version,
                                image_size, 0)
            + _field(board_env, BOARD_ENV_SIZE, "board_env")
            + _field(project, PROJECT_SIZE, "project")
            + _field(version_string, VERSION_STRING_SIZE, "version string")
            + image_sha256 + partition_prefix)
    assert len(body) == BODY_SIZE
    return body


def decode_body(body: bytes) -> Dict[str, object]:
    """@return the fields of a 128-byte manifest body."""
    if len(body) != BODY_SIZE or body[:4] != MAGIC or body[4] != FORMAT_VERSION:
        raise ManifestError("not a manifest body of this format")
    image_type, flags, key_id, version, image_size, _ = struct.unpack_from("<BHIIII", body, 5)

    def text(offset: int, size: int) -> str:
        return body[offset:offset + size].split(b"\0", 1)[0].decode("ascii")

    return {
        "image_type": image_type, "flags": flags, "key_id": key_id, "version": version,
        "image_size": image_size, "board_env": text(24, BOARD_ENV_SIZE),
        "project": text(40, PROJECT_SIZE), "version_string": text(56, VERSION_STRING_SIZE),
        "image_sha256": body[88:120].hex(), "partition_prefix": body[120:128].hex(),
    }


def load_key(path: Path) -> ec.EllipticCurvePrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise ManifestError(f"{path} is not an ECDSA P-256 private key")
    return key


def load_public_key(path: Path) -> bytes:
    """@return the uncompressed public key of a private or public PEM key file."""
    data = path.read_bytes()
    try:
        return public_key_bytes(load_key(path))
    except (ValueError, TypeError, ManifestError):
        key = serialization.load_pem_public_key(data)
    if not isinstance(key, ec.EllipticCurvePublicKey) or key.curve.name != "secp256r1":
        raise ManifestError(f"{path} is not an ECDSA P-256 key")
    return key.public_bytes(serialization.Encoding.X962,
                            serialization.PublicFormat.UncompressedPoint)


def show(manifest_path: Path, key_path: Optional[Path]) -> bool:
    """Prints the fields of a signed manifest. @return false if a key is given and fails."""
    signed = manifest_path.read_bytes()
    if len(signed) < BODY_SIZE + SIGNATURE_SIZE:
        raise ManifestError(f"{manifest_path} is {len(signed)} bytes, a manifest is 192")
    for name, value in decode_body(signed[:BODY_SIZE]).items():
        shown = f"0x{value:08X}" if name in ("key_id", "version") else value
        print(f"{name}: {shown}")
    if key_path is None:
        return True
    valid = verify(signed, load_public_key(key_path))
    print(f"signature: {'valid' if valid else 'INVALID'} for {key_path}")
    return valid


def public_key_bytes(key: ec.EllipticCurvePrivateKey) -> bytes:
    """@return the uncompressed public key (0x04 || X || Y, 65 bytes)."""
    return key.public_key().public_bytes(serialization.Encoding.X962,
                                         serialization.PublicFormat.UncompressedPoint)


def sign_body(body: bytes, key: ec.EllipticCurvePrivateKey) -> bytes:
    """@return the raw r || s signature (64 bytes) over SHA-256(body)."""
    r, s = decode_dss_signature(key.sign(body, ec.ECDSA(hashes.SHA256())))
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify(signed: bytes, public_key: bytes) -> bool:
    """@return true if the 192-byte manifest is signed by @p public_key."""
    point = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), public_key)
    body, raw = signed[:BODY_SIZE], signed[BODY_SIZE:BODY_SIZE + SIGNATURE_SIZE]
    signature = encode_dss_signature(int.from_bytes(raw[:32], "big"),
                                     int.from_bytes(raw[32:], "big"))
    try:
        point.verify(signature, body, ec.ECDSA(hashes.SHA256()))
        return True
    except Exception:  # cryptography raises InvalidSignature
        return False


def sign_release(release_dir: Path, key: ec.EllipticCurvePrivateKey, key_id: int,
                 flags: int = 0) -> bytes:
    """Writes manifest.bin and bundle.bin into @p release_dir. @return the signed manifest."""
    info = json.loads((release_dir / "info.json").read_text())
    image = (release_dir / "firmware.bin").read_bytes()
    if len(image) != info["image_size"] or image[-32:].hex() != info["image_sha256"]:
        raise ManifestError(f"{release_dir / 'firmware.bin'} does not match info.json")
    body = encode_body(key_id=key_id, version=info["version_u32"], image_size=len(image),
                       board_env=info["env"], project=info["project"],
                       version_string=info["version"],
                       image_sha256=bytes.fromhex(info["image_sha256"]),
                       partition_prefix=bytes.fromhex(info["partitions_sha256"])[:8],
                       flags=flags)
    signed = body + sign_body(body, key)
    (release_dir / "manifest.bin").write_bytes(signed)
    (release_dir / "bundle.bin").write_bytes(signed + image)
    return signed


def c_array(data: bytes, indent: str = "    ") -> str:
    lines = []
    for i in range(0, len(data), 12):
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in data[i:i + 12]) + ",")
    return "\n".join(lines)


def keygen(out: Path) -> bytes:
    if out.exists():
        raise ManifestError(f"{out} exists; refusing to overwrite a key")
    key = ec.generate_private_key(ec.SECP256R1())
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(pem)
    return public_key_bytes(key)


def write_vectors(key: ec.EllipticCurvePrivateKey, out_dir: Path,
                  firmware_header: Optional[Path] = FIRMWARE_TEST_KEY_HEADER) -> None:
    """Writes manifest_vectors.json, manifestVectors.h and the firmware test-key header."""
    other = ec.generate_private_key(ec.SECP256R1())
    fields = dict(key_id=TEST_KEY_ID, version=0x00010300, image_size=1182800, board_env="tbeam",
                  project="LoRaChat", version_string="0.1.3+gabc1234",
                  image_sha256=bytes(range(32)), partition_prefix=bytes(range(0xA0, 0xA8)),
                  flags=FLAG_SKIP_MESH_CHECK)
    body = encode_body(**fields)
    valid = body + sign_body(body, key)
    tampered = bytearray(valid)
    tampered[60] ^= 0x01  # one bit of the version string
    vectors = {
        "valid": (valid, True),
        "tampered_body": (bytes(tampered), False),
        "signed_by_other_key": (body + sign_body(body, other), False),
        "unsigned": (body + bytes(SIGNATURE_SIZE), False),
    }
    public = public_key_bytes(key)
    for name, (data, expected) in vectors.items():
        assert verify(data, public) == expected, name

    json_out = {
        "public_key": public.hex(),
        "key_id": TEST_KEY_ID,
        "fields": {k: (v.hex() if isinstance(v, bytes) else v) for k, v in fields.items()},
        "vectors": {name: {"manifest": data.hex(), "valid": expected}
                    for name, (data, expected) in vectors.items()},
    }
    (out_dir / "manifest_vectors.json").write_text(json.dumps(json_out, indent=2) + "\n")

    parts = ["// Generated by scripts/ota_tools/sign_manifest.py vectors. Do not edit.",
             "#pragma once", "", "#include <cstdint>", "",
             f"constexpr uint32_t TEST_KEY_ID = 0x{TEST_KEY_ID:08X};", "",
             "constexpr uint8_t TEST_PUBLIC_KEY[65] = {", c_array(public), "};"]
    for name, (data, _) in vectors.items():
        parts += ["", f"constexpr uint8_t VECTOR_{name.upper()}[192] = {{", c_array(data), "};"]
    (out_dir / "manifestVectors.h").write_text("\n".join(parts) + "\n")

    if firmware_header is not None:
        firmware = ["// Generated by scripts/ota_tools/sign_manifest.py vectors. Do not edit.",
                    "// Test key (bench and CI images) and a manifest it signed, used by the boot",
                    "// self-test to check the signature path.",
                    "#pragma once", "", "#include <cstdint>", "",
                    f"constexpr uint32_t OTA_TEST_KEY_ID = 0x{TEST_KEY_ID:08X};", "",
                    "constexpr uint8_t OTA_TEST_PUBLIC_KEY[65] = {", c_array(public), "};", "",
                    "constexpr uint8_t OTA_SELF_TEST_MANIFEST[192] = {", c_array(valid), "};"]
        firmware_header.write_text("\n".join(firmware) + "\n")


def main(argv: Optional[list] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("keygen")
    gen.add_argument("--out", type=Path, required=True)
    sign = commands.add_parser("sign")
    sign.add_argument("release_dir", type=Path)
    sign.add_argument("--key", type=Path, required=True)
    sign.add_argument("--key-id", type=lambda v: int(v, 0), required=True)
    sign.add_argument("--allow-downgrade", action="store_true")
    sign.add_argument("--skip-mesh-check", action="store_true")
    sh = commands.add_parser("show")
    sh.add_argument("manifest", type=Path)
    sh.add_argument("--key", type=Path, help="private or public PEM key to check the signature")
    vec = commands.add_parser("vectors")
    vec.add_argument("--key", type=Path, default=TEST_KEY)
    vec.add_argument("--out", type=Path, default=VECTORS_DIR)
    args = parser.parse_args(argv)

    try:
        if args.command == "keygen":
            public = keygen(args.out)
            print(f"Private key written to {args.out}. Keep it offline.")
            print("Public key for src/ota/otaKeys.cpp:")
            print(c_array(public))
        elif args.command == "sign":
            flags = ((FLAG_ALLOW_DOWNGRADE if args.allow_downgrade else 0)
                     | (FLAG_SKIP_MESH_CHECK if args.skip_mesh_check else 0))
            sign_release(args.release_dir, load_key(args.key), args.key_id, flags)
            print(f"{args.release_dir}: manifest.bin and bundle.bin written")
        elif args.command == "show":
            if not show(args.manifest, args.key):
                return 1
        else:
            write_vectors(load_key(args.key), args.out)
            print(f"test vectors written to {args.out}")
    except (OSError, ValueError, KeyError, ManifestError) as exc:
        print(f"sign_manifest: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
