import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import otadata  # noqa: E402
from otadata import OTADATA_SIZE, SECTOR_SIZE, State  # noqa: E402

FRESH = b"\xff" * OTADATA_SIZE


def test_crc_matches_idf_otatool_formula():
    # Same expression as components/app_update/otatool.py in IDF 4.4.
    import binascii
    import struct

    for seq in (1, 2, 3, 0x1234):
        expected = binascii.crc32(struct.pack("I", seq), 0xFFFFFFFF) % (1 << 32)
        assert otadata.seq_crc(seq) == expected


def test_fresh_otadata_has_no_active_copy():
    assert otadata.active(otadata.decode(FRESH)) is None


def test_select_on_fresh_otadata_boots_requested_slot():
    out = otadata.select(FRESH, slot=1)
    _, entry = otadata.active(otadata.decode(out))
    assert entry.boot_slot() == 1
    assert entry.state == State.NEW


def test_select_keeps_active_copy_and_writes_the_other_sector():
    first = otadata.select(FRESH, slot=0, state=State.VALID)
    second = otadata.select(first, slot=1)
    entries = otadata.decode(second)
    assert entries[0].seq == 1 and entries[0].state == State.VALID
    assert entries[1].seq == 2 and entries[1].state == State.NEW
    index, entry = otadata.active(entries)
    assert index == 1 and entry.boot_slot() == 1


def test_seq_grows_strictly_even_for_the_same_slot():
    image = otadata.select(FRESH, slot=1, state=State.VALID)
    again = otadata.select(image, slot=1)
    entries = otadata.decode(again)
    _, entry = otadata.active(entries)
    assert entry.seq == 4
    assert entry.boot_slot() == 1


def test_aborted_copy_is_skipped_with_rollback():
    image = otadata.select(FRESH, slot=0, state=State.VALID)
    image = otadata.select(image, slot=1, state=State.ABORTED)
    _, entry = otadata.active(otadata.decode(image))
    assert entry.boot_slot() == 0
    _, entry = otadata.active(otadata.decode(image), rollback=False)
    assert entry.boot_slot() == 1


def test_copy_with_bad_crc_is_ignored():
    image = bytearray(otadata.select(FRESH, slot=1))
    image[28] ^= 0xFF
    assert otadata.active(otadata.decode(bytes(image))) is None


def test_entries_live_at_sector_starts():
    image = otadata.select(otadata.select(FRESH, slot=0, state=State.VALID), slot=1)
    assert image[SECTOR_SIZE:SECTOR_SIZE + 4] == (2).to_bytes(4, "little")
    assert image[32:SECTOR_SIZE] == b"\xff" * (SECTOR_SIZE - 32)


def test_rejects_short_input_and_bad_slot():
    with pytest.raises(ValueError):
        otadata.decode(b"\xff" * 10)
    with pytest.raises(ValueError):
        otadata.select(FRESH, slot=2)


def test_cli_round_trip(tmp_path):
    src = tmp_path / "otadata.bin"
    dst = tmp_path / "new.bin"
    src.write_bytes(FRESH)
    assert otadata.main(["select", str(src), "--slot", "1", "-o", str(dst)]) == 0
    assert otadata.main(["decode", str(dst)]) == 0
    _, entry = otadata.active(otadata.decode(dst.read_bytes()))
    assert entry.boot_slot() == 1
