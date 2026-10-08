import hashlib
import json
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import release  # noqa: E402

SLOT = 0x1C0000
PARTITIONS = (
    "# Name,   Type, SubType, Offset,   Size,     Flags\n"
    "nvs,      data, nvs,     0x9000,   0x5000,\n"
    "app0,     app,  ota_0,   0x10000,  0x1C0000,\n"
)


def esp_image(version="0.1.0+gabc1234", size=4096, elf_sha=b"\x11" * 32):
    header = bytearray(24)
    header[0] = release.IMAGE_MAGIC
    header[23] = 1
    desc = bytearray(256)
    struct.pack_into("<I", desc, 0, release.APP_DESC_MAGIC)
    desc[16:16 + len(version)] = version.encode()
    desc[48:56] = b"LoRaChat"
    desc[144:176] = elf_sha
    body = bytes(header) + bytes(8) + bytes(desc)
    body += bytes(size - 32 - len(body))
    return body + hashlib.sha256(body).digest()


def make_build(tmp_path, image=None, version_txt="0.1.0"):
    project = tmp_path / "project"
    build = tmp_path / "build"
    project.mkdir()
    build.mkdir()
    (project / "version.txt").write_text(version_txt + "\n")
    (project / "partitions.csv").write_text(PARTITIONS)
    (build / "firmware.bin").write_bytes(image if image is not None else esp_image())
    (build / "bootloader.bin").write_bytes(esp_image(size=1024))
    (build / "partitions.bin").write_bytes(b"\xaa" * 3072)
    (build / "ota_data_initial.bin").write_bytes(b"\xff" * 8192)
    (build / "firmware.elf").write_bytes(b"elf")
    return project, build


@pytest.fixture(autouse=True)
def no_version_override(monkeypatch):
    monkeypatch.delenv("FW_VERSION", raising=False)


def test_collects_images_and_writes_info(tmp_path):
    project, build = make_build(tmp_path)
    out = release.collect("tbeam", build, tmp_path / "release", project, None, False)
    assert out == tmp_path / "release" / "tbeam" / "0.1.0+gabc1234"
    info = json.loads((out / "info.json").read_text())
    image = (build / "firmware.bin").read_bytes()
    assert info["image_sha256"] == image[-32:].hex()
    assert info["version_u32"] == 0x00010000
    assert info["elf_sha256"] == "11" * 32
    assert info["slot_size"] == SLOT
    for name in release.IMAGE_FILES + ("firmware.elf",):
        assert (out / name).exists()


def test_tag_is_appended_to_the_directory(tmp_path):
    project, build = make_build(tmp_path)
    out = release.collect("tbeam", build, tmp_path / "release", project, "t4", False)
    assert out.name == "0.1.0+gabc1234-t4"


def test_rejects_corrupt_appended_hash(tmp_path):
    image = bytearray(esp_image())
    image[100] ^= 0xFF
    project, build = make_build(tmp_path, image=bytes(image))
    with pytest.raises(release.ReleaseError, match="does not match"):
        release.check_build(build, project, False)


def test_rejects_version_other_than_version_txt(tmp_path):
    project, build = make_build(tmp_path, version_txt="0.2.0")
    with pytest.raises(release.ReleaseError, match="does not match 0.2.0"):
        release.check_build(build, project, False)


def test_fw_version_overrides_version_txt(tmp_path, monkeypatch):
    project, build = make_build(tmp_path, image=esp_image(version="0.1.1+gabc1234"))
    monkeypatch.setenv("FW_VERSION", "0.1.1")
    assert release.check_build(build, project, False)["version_u32"] == 0x00010100


def test_rejects_dirty_build_unless_allowed(tmp_path):
    project, build = make_build(tmp_path, image=esp_image(version="0.1.0+gabc1234.dirty"))
    with pytest.raises(release.ReleaseError, match="uncommitted"):
        release.check_build(build, project, False)
    assert release.check_build(build, project, True)["version"] == "0.1.0+gabc1234.dirty"


def test_rejects_image_over_the_size_gate(tmp_path):
    project, build = make_build(tmp_path, image=esp_image(size=int(SLOT * 0.9) + 16))
    with pytest.raises(release.ReleaseError, match="over 90%"):
        release.check_build(build, project, False)


def test_rejects_missing_file(tmp_path):
    project, build = make_build(tmp_path)
    (build / "bootloader.bin").unlink()
    with pytest.raises(release.ReleaseError, match="bootloader.bin is missing"):
        release.check_build(build, project, False)


@pytest.mark.parametrize("version", ["1.2", "1.2.3.4", "256.0.0", "v1.0.0"])
def test_version_u32_rejects_bad_versions(version):
    with pytest.raises(release.ReleaseError):
        release.version_u32(version)


def test_default_build_dir_follows_the_pio_sh_workspace(monkeypatch, tmp_path):
    for name in ("PLATFORMIO_BUILD_DIR", "PLATFORMIO_WORKSPACE_DIR", "XDG_CACHE_HOME"):
        monkeypatch.delenv(name, raising=False)
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path))
    path = release.default_build_dir("tbeam", release.Path("/mnt/d/x/LoRaChat-ota-f1"))
    assert path == tmp_path / "pio-ws" / "LoRaChat-ota-f1" / "build" / "tbeam"
    assert release.default_build_dir("tbeam", tmp_path / "proj") == (
        tmp_path / "proj" / ".pio" / "build" / "tbeam")
