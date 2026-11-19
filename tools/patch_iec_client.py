#!/usr/bin/env python3
"""Generate an offline V670 client-only experiment. Never modifies input or TV.
Not a working IEC61937 implementation; requires a separately validated HAL/policy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

STOCK_SHA256 = 'dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9'
STOCK_SIZE = 702872
# VA, original Thumb bytes (little endian), target of existing normal path, purpose.
SITES = (
    (0x5c636, '40f0ca80', 0x5c7ce, 'offload capability query'),
    (0x67f58, '40f0d280', 0x68100, 'direct playback capability query'),
    (0x68932, 'acb1', 0x68972, 'return policy profiles without client-side filtering'),
    (0x6d862, '73d1', 0x6d94c, 'AudioTrack::set normal format validation'),
)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def exec_offset(data, va, width):
    if data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
        raise ValueError('Expected little-endian ARM ELF32')
    phoff = struct.unpack_from('<I', data, 28)[0]
    entsize, count = struct.unpack_from('<HH', data, 42)
    if entsize != 32:
        raise ValueError('Unexpected ELF program header size')
    offsets = []
    for i in range(count):
        typ, off, addr, _, filesz, _, flags, _ = struct.unpack_from('<8I', data, phoff + i * entsize)
        if typ == 1 and flags & 1 and addr <= va and va + width <= addr + filesz:
            offsets.append(off + va - addr)
    if len(offsets) != 1 or offsets[0] + width > len(data):
        raise ValueError('VA not uniquely mapped to executable file bytes')
    return offsets[0]

def branch16(va, target, width):
    delta = target - (va + 4)
    if delta % 2 or not -2048 <= delta <= 2046 or width not in (2, 4):
        raise ValueError('Invalid Thumb B target/width')
    return struct.pack('<H', 0xe000 | ((delta // 2) & 0x7ff)) + (b'\x00\xbf' if width == 4 else b'')

def patch(original):
    if len(original) != STOCK_SIZE or digest(original) != STOCK_SHA256:
        raise ValueError('Refusing different input; exact V670 client required')
    modified = bytearray(original)
    changes = []
    for va, old_hex, target, purpose in SITES:
        old = bytes.fromhex(old_hex)
        off = exec_offset(original, va, len(old))
        exec_offset(original, target, 2)
        if original[off:off + len(old)] != old:
            raise ValueError('Unexpected original instruction at ' + hex(va))
        new = branch16(va, target, len(old))
        modified[off:off + len(old)] = new
        changes.append(dict(va=hex(va), file_offset=hex(off), target=hex(target), before=old.hex(), after=new.hex(), purpose=purpose))
    allowed = set()
    for c in changes:
        off = int(c['file_offset'], 16)
        allowed.update(range(off, off + len(bytes.fromhex(c['before']))))
    diff = [i for i, (a, b) in enumerate(zip(original, modified)) if a != b]
    assert len(modified) == len(original) and set(diff) <= allowed
    return bytes(modified), dict(stock_sha256=STOCK_SHA256, output_sha256=digest(modified), bytes=len(modified), changed_bytes=len(diff), changes=changes, runtime_validated=False, flashable=False, scope='client-only experiment; HAL and policy unchanged')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stock_client', type=Path)
    p.add_argument('--out-dir', type=Path, default=Path('private/iec_client_experiment'))
    args = p.parse_args()
    modified, report = patch(args.stock_client.read_bytes())
    args.out_dir.mkdir(parents=True, exist_ok=False)
    output = args.out_dir / 'libaudioclient.experimental.so'
    output.write_bytes(modified)
    (args.out_dir / 'patch_report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Saved offline experiment:', output)
    print('NOT FLASHABLE. No HAL/policy changes; do not deploy.')

if __name__ == '__main__':
    main()
