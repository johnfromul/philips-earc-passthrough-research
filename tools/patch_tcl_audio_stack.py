#!/usr/bin/env python3
"""
Helper utilities for building and porting the TCL MT5896 eARC TrueHD Atmos MAT bypass:
  1. extract-crcs <ko1> [ko2 ...]
       Extracts __versions CRCs and vermagic from pulled /vendor_dlkm/lib/modules/*.ko.
  2. patch-xml <in.xml> <out.xml>
       Adds direct_iec61937 output mixPort + HDMI EARC route & profile (preserving dynamic profiles).
  3. patch-audioclient <in_libaudioclient.so> <out_libaudioclient.so>
       Removes TCL's 5 hardcoded "/system/bin/mediaserver" package-name blocks for AUDIO_FORMAT_IEC61937
       in 32-bit Thumb-2 libaudioclient.so by scanning for cmp.w rX, #0xd000000 guards.
"""

import struct
import sys
import xml.etree.ElementTree as ET

NEEDED_SYMS = [
    b"module_layout",
    b"_printk",
    b"__ioremap",
    b"iounmap",
    b"vmalloc",
    b"vfree",
    b"usleep_range_state",
    b"msleep",
    b"arm64_use_ng_mappings",
    b"misc_register",
    b"misc_deregister",
    b"_copy_from_iter",
    b"kthread_create_on_node",
    b"wake_up_process",
]


def cmd_extract_crcs(paths):
    found = {}
    vermagic = None
    for p in paths:
        data = open(p, "rb").read()
        if vermagic is None:
            vm = data.find(b"vermagic=")
            if vm != -1:
                end = data.find(b"\x00", vm)
                vermagic = data[vm:end].decode("ascii", errors="replace")
        for sym in NEEDED_SYMS:
            needle = sym + b"\x00"
            idx = 0
            while True:
                idx = data.find(needle, idx)
                if idx == -1:
                    break
                if idx >= 8:
                    crc = struct.unpack("<Q", data[idx - 8 : idx])[0]
                    if 0 < crc < 0x100000000:
                        found[sym.decode()] = crc
                idx += 1
    if vermagic:
        print(f"vermagic: {vermagic}")
    print("\n__versions CRCs:")
    for sym in NEEDED_SYMS:
        name = sym.decode()
        if name in found:
            print(f'    {{ 0x{found[name]:08x}ULL, "{name}" }},')
        else:
            print(f'    /* MISSING: {name} */')


def cmd_patch_xml(in_path, out_path):
    src = open(in_path, "r", encoding="utf-8").read()

    if 'name="direct_iec61937 output"' not in src:
        mix_add = (
            '        <mixPort name="direct_iec61937 output" role="source"\n'
            '                 flags="AUDIO_OUTPUT_FLAG_DIRECT" maxOpenCount="1" maxActiveCount="1">\n'
            '            <profile name="" format="AUDIO_FORMAT_IEC61937"\n'
            '                     samplingRates="192000"\n'
            '                     channelMasks="AUDIO_CHANNEL_OUT_7POINT1"/>\n'
            '        </mixPort>\n'
        )
        anchor = '        <mixPort name="direct_multichannel_pcm output"'
        if anchor not in src:
            anchor = "    </mixPorts>"
        src = src.replace(anchor, mix_add + anchor, 1)

    # Add direct_iec61937 output to HDMI EARC route
    import re
    route_re = re.compile(
        r'(<route\s+type="mix"\s+sink="HDMI EARC"\s+sources=")([^"]+)("/>)'
    )
    m = route_re.search(src)
    if not m:
        raise RuntimeError("Could not locate HDMI EARC <route> in XML")
    if "direct_iec61937 output" not in m.group(2):
        src = (
            src[: m.start(2)]
            + m.group(2)
            + ",direct_iec61937 output"
            + src[m.end(2) :]
        )

    # Add AUDIO_FORMAT_DEFAULT (preserve dynamic EDID profiles) + AUDIO_FORMAT_IEC61937 to HDMI EARC devicePort
    earc_re = re.compile(
        r'(<devicePort\s+tagName="HDMI EARC"\s+type="AUDIO_DEVICE_OUT_HDMI_EARC"\s+role="sink">)(\s*)(</devicePort>)'
    )
    m2 = earc_re.search(src)
    if not m2:
        raise RuntimeError("Could not locate empty HDMI EARC <devicePort> in XML")
    earc_body = (
        '\n            <profile name="" format="AUDIO_FORMAT_DEFAULT"/>\n'
        '            <profile name="" format="AUDIO_FORMAT_IEC61937"\n'
        '                     samplingRates="192000"\n'
        '                     channelMasks="AUDIO_CHANNEL_OUT_7POINT1"/>\n        '
    )
    src = src[: m2.start(2)] + earc_body + src[m2.end(2) :]

    # Validate XML well-formedness
    ET.fromstring(src)
    open(out_path, "w", encoding="utf-8", newline="\n").write(src)
    print(f"Patched XML written and validated: {out_path}")


def cmd_patch_audioclient(in_path, out_path):
    """
    Finds TCL's 5 Thumb-2 AUDIO_FORMAT_IEC61937 (0x0D000000) package-name guards:
      - cmp.w r0, #0xd000000 (b0 f1 50 6f) followed by bne.w (40 f0 XX 80) -> b.w (00 f0 XX b8)
      - cmp.w r0, #0xd000000 (b0 f1 50 6f) followed by beq.n (XX d0) inside remove_if -> nop (00 bf)
      - cmp.w r9, #0xd000000 (b9 f1 50 6f) followed by bne.n (XX d1) in AudioTrack::set -> b.n (XX e0)
    Verifies the library actually contains TCL's "discovery this package" strings first.
    """
    data = bytearray(open(in_path, "rb").read())
    if b"discovery this package" not in data:
        raise RuntimeError("Input libaudioclient.so does not contain TCL's IEC61937 package block")

    patched = 0
    i = 0
    n = len(data)
    while i < n - 8:
        # Pattern A: cmp.w r0, #0xd000000 (b0 f1 50 6f)
        if data[i : i + 4] == b"\xb0\xf1\x50\x6f":
            hw1, hw2 = struct.unpack_from("<HH", data, i + 4)
            # Case A1: bne.w +imm (hw1 == 0xf040, hw2 & 0xd000 == 0x8000) followed by blx (proc/cmdline check)
            if hw1 == 0xF040 and (hw2 & 0xF800) == 0x8000:
                new_hw1 = 0xF000
                new_hw2 = (hw2 & 0x07FF) | 0xB800
                struct.pack_into("<HH", data, i + 4, new_hw1, new_hw2)
                print(f"  [file+0x{i+4:x}] bne.w -> b.w (0x{hw1:04x} 0x{hw2:04x} -> 0x{new_hw1:04x} 0x{new_hw2:04x})")
                patched += 1
            # Case A2: beq +0xa (05 d0) in getDirectProfilesForAttributes remove_if
            elif hw1 == 0xD005:
                struct.pack_into("<H", data, i + 4, 0xBF00)
                print(f"  [file+0x{i+4:x}] beq +0xa -> nop")
                patched += 1
        # Pattern B: cmp.w r9, #0xd000000 (b9 f1 50 6f) followed by bne.n (XX d1) in AudioTrack::set
        elif data[i : i + 4] == b"\xb9\xf1\x50\x6f":
            hw1 = struct.unpack_from("<H", data, i + 4)[0]
            if (hw1 & 0xFF00) == 0xD100:
                new_hw1 = (hw1 & 0x00FF) | 0xE000
                struct.pack_into("<H", data, i + 4, new_hw1)
                print(f"  [file+0x{i+4:x}] bne.n -> b.n (0x{hw1:04x} -> 0x{new_hw1:04x})")
                patched += 1
        i += 2

    if patched < 4:
        raise RuntimeError(f"Expected >= 4 TCL IEC61937 guards, only found {patched}; inspect disassembly manually")

    open(out_path, "wb").write(data)
    print(f"Patched {patched} IEC61937 guards -> {out_path}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "extract-crcs":
        cmd_extract_crcs(sys.argv[2:])
    elif cmd == "patch-xml" and len(sys.argv) == 4:
        cmd_patch_xml(sys.argv[2], sys.argv[3])
    elif cmd == "patch-audioclient" and len(sys.argv) == 4:
        cmd_patch_audioclient(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        sys.exit(1)
