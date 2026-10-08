#!/usr/bin/env python3
"""Encode and decode the ESP-IDF 4.4 otadata partition.

otadata holds two copies of esp_ota_select_entry_t, one per flash sector:
    uint32 ota_seq, uint8 seq_label[20], uint32 ota_state, uint32 crc (CRC32 of ota_seq only)

The bootloader boots slot (seq - 1) % ota_count of the valid copy with the highest seq. With app
rollback enabled it skips copies in INVALID or ABORTED state and turns NEW into PENDING_VERIFY.

Bench usage (read the current otadata, select ota_1 as NEW, write it back):
    esptool.py read_flash 0xe000 0x2000 otadata.bin
    python otadata.py select otadata.bin --slot 1 -o otadata_new.bin
    esptool.py write_flash 0xe000 otadata_new.bin
"""

import argparse
import binascii
import struct
import sys
from dataclasses import dataclass
from enum import IntEnum
from typing import List, Optional, Tuple

OTADATA_OFFSET = 0xE000
OTADATA_SIZE = 0x2000
SECTOR_SIZE = 0x1000
OTA_COUNT = 2
SEQ_UNSET = 0xFFFFFFFF

ENTRY = struct.Struct("<I20sII")


class State(IntEnum):
    NEW = 0
    PENDING_VERIFY = 1
    VALID = 2
    INVALID = 3
    ABORTED = 4
    UNDEFINED = 0xFFFFFFFF


@dataclass
class Entry:
    seq: int
    label: bytes
    state: int
    crc: int

    @property
    def valid(self) -> bool:
        return self.seq != SEQ_UNSET and self.crc == seq_crc(self.seq)

    @property
    def state_name(self) -> str:
        try:
            return State(self.state).name
        except ValueError:
            return f"0x{self.state:08x}"

    def boot_slot(self, ota_count: int = OTA_COUNT) -> int:
        return (self.seq - 1) % ota_count

    def pack(self) -> bytes:
        return ENTRY.pack(self.seq, self.label, self.state, self.crc)


def seq_crc(seq: int) -> int:
    """CRC used by the bootloader: esp_rom_crc32_le(UINT32_MAX, &ota_seq, 4)."""
    return binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF


def make_entry(seq: int, state: State) -> Entry:
    return Entry(seq, b"\xff" * 20, int(state), seq_crc(seq))


def decode(data: bytes) -> List[Entry]:
    if len(data) < OTADATA_SIZE:
        raise ValueError(f"otadata must be {OTADATA_SIZE} bytes, got {len(data)}")
    return [Entry(*ENTRY.unpack_from(data, i * SECTOR_SIZE)) for i in range(2)]


def active(entries: List[Entry], rollback: bool = True) -> Optional[Tuple[int, Entry]]:
    """Returns (copy index, entry) the bootloader would use, or None for a fresh otadata."""
    skipped = (State.INVALID, State.ABORTED) if rollback else ()
    candidates = [(i, e) for i, e in enumerate(entries) if e.valid and e.state not in skipped]
    if not candidates:
        return None
    return max(candidates, key=lambda c: c[1].seq)


def next_seq(base_seq: int, slot: int, ota_count: int = OTA_COUNT) -> int:
    """Smallest seq greater than base_seq that boots @p slot."""
    seq = base_seq + 1
    while (seq - 1) % ota_count != slot:
        seq += 1
    return seq


def select(data: bytes, slot: int, state: State = State.NEW) -> bytes:
    """Returns a new otadata image that boots @p slot in @p state, keeping the active copy."""
    if not 0 <= slot < OTA_COUNT:
        raise ValueError(f"slot must be in 0..{OTA_COUNT - 1}")
    entries = decode(data)
    current = active(entries)
    base_seq = current[1].seq if current else 0
    target = 1 - current[0] if current else 0

    out = bytearray(b"\xff" * OTADATA_SIZE)
    if current:
        out[current[0] * SECTOR_SIZE:current[0] * SECTOR_SIZE + ENTRY.size] = current[1].pack()
    entry = make_entry(next_seq(base_seq, slot), state)
    out[target * SECTOR_SIZE:target * SECTOR_SIZE + ENTRY.size] = entry.pack()
    return bytes(out)


def describe(data: bytes) -> str:
    entries = decode(data)
    lines = []
    for i, e in enumerate(entries):
        status = "valid" if e.valid else "invalid"
        seq = "unset" if e.seq == SEQ_UNSET else str(e.seq)
        slot = f"ota_{e.boot_slot()}" if e.valid else "-"
        lines.append(f"copy {i}: seq={seq} slot={slot} state={e.state_name} ({status})")
    current = active(entries)
    if current:
        lines.append(f"boots: ota_{current[1].boot_slot()} state={current[1].state_name}")
    else:
        lines.append("boots: ota_0 (fresh otadata)")
    return "\n".join(lines)


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_decode = sub.add_parser("decode", help="print both otadata copies")
    p_decode.add_argument("file")

    p_select = sub.add_parser("select", help="write a copy that boots a slot")
    p_select.add_argument("file", help="current otadata read from the board")
    p_select.add_argument("--slot", type=int, required=True)
    p_select.add_argument("--state", default="new", choices=[s.name.lower() for s in State])
    p_select.add_argument("-o", "--output", required=True)

    args = parser.parse_args(argv)
    with open(args.file, "rb") as f:
        data = f.read()

    if args.cmd == "decode":
        print(describe(data))
        return 0

    out = select(data, args.slot, State[args.state.upper()])
    with open(args.output, "wb") as f:
        f.write(out)
    print(describe(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
