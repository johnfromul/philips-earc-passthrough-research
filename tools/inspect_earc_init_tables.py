#!/usr/bin/env python3
"""Inspect eARC-related init records in the exact stock V670 offload module."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

MODULE_SHA256 = "0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4"
RODATA_FILE_OFFSET = 0x1FD90
TABLES = {
    "AudioPreInitTbl_Clock": (0x3B0FC, 0x4B8),
    "AudioInitTbl_0": (0x3B5B4, 0x6F8),
    "AudioInitTbl_SwSetting": (0x3BCAC, 0x238),
    "EarcInitTbl": (0x3CC44, 0x38),
}
BANKS = {
    0x950300: "Vivaldi3",
    0x951100: "Vivaldi01_1",
    0x172C00: "C9K eARC TX",
    0xE24000: "Alternate eARC TX",
}
RECORD = struct.Struct("<IBB2x")


def inspect(data):
    if hashlib.sha256(data).hexdigest() != MODULE_SHA256:
        raise ValueError("Unexpected SHA-256 for the audited V670 SoC module")

    report = {}
    for name, (offset, size) in TABLES.items():
        start = RODATA_FILE_OFFSET + offset
        records = []
        for index, (reg, mask, value) in enumerate(RECORD.iter_unpack(data[start:start + size])):
            if reg == 0xFFFFFF and mask == 0:
                break
            bank = BANKS.get(reg & ~0xFF)
            if bank:
                records.append({
                    "index": index,
                    "bank": bank,
                    "register": hex(reg),
                    "mask": hex(mask),
                    "value": hex(value),
                })
        report[name] = records
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.module.read_bytes()), indent=2))
