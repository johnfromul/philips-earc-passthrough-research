#!/usr/bin/env python3
"""Reproduce read-only MAT branch-table observations on the exact stock module."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

SHA = "0720c8c96dc6a84b112ade4877058aaa7caa37158830e201d148e7ff57e5b7e4"

def inspect(data):
    if hashlib.sha256(data).hexdigest() != SHA:
        raise ValueError("Not the audited stock V670 US AU02 soc module")
    # Exact-image offsets; addresses in report are section-relative ET_REL offsets.
    ro = memoryview(data)[0x1fd90:0x1fd90 + 0x3f5b0]
    def target(table, base, index, fmt, stride=1):
        return base + stride * struct.unpack_from(fmt, ro, table + index * struct.calcsize(fmt))[0]
    report = {
        "module_sha256": SHA,
        "mat_codec": "0x1b",
        "auto_mat_dispatch": hex(target(0x1584, 0x79a84, 0x1b-2, "<i")),
        "bypass_mat_dispatch": hex(target(0x1710, 0x7a194, 0x1b-2, "<i")),
        "shm_mat_hdmi_bypass_dispatch": hex(target(0x21a0, 0x90d24, 0xb4, "<H", 4)),
    }
    assert report["auto_mat_dispatch"] == "0x79a94"
    assert report["bypass_mat_dispatch"] == "0x7a46c"
    assert report["shm_mat_hdmi_bypass_dispatch"] == "0x91744"
    # Split BTF: only resolve local names, not missing base kernel type IDs.
    b = memoryview(data)[0x121d88:0x121d88+421618]
    magic, ver, flags, hdr, toff, tlen, soff, slen = struct.unpack_from("<HBBIIIII", b)
    if magic != 0xeb9f or ver != 1:
        raise ValueError("Unexpected BTF header")
    types = b[hdr+toff:hdr+toff+tlen]
    strings = bytes(b[hdr+soff:hdr+soff+slen])
    base = 0x206234
    if not strings.startswith(b"mtk_soc_priv\0riu_base\0bank_num\0reg_bank\0"):
        raise ValueError("Unexpected local split BTF strings")
    def name(offset):
        pos = offset-base
        return strings[pos:strings.find(b"\0", pos)].decode("utf-8") if 0 <= pos < len(strings) else ""
    enums, fields = {}, {}
    wanted = {"E_AUDIO_INFO_HDMI_IN", "E_AUDIO_INFO_MM_IN", "AU_DVB_STANDARD_MAT",
              "Audio_infoType_Is_Atmos_Stream", "Audio_infoType_Mat_Stream_Type",
              "R2_SHM_PARAM_DOLBY_MAT_HDMI_BYPASS"}
    selected = {"eAudioSource", "g_bNextEnableMatBypass", "g_bEnableMatBypass",
                "g_bNextEnableMatEncode", "g_bSupportMs12MatEncoder", "g_bIsEARCDevice"}
    pos = 0
    while pos < len(types):
        no, info, size = struct.unpack_from("<III", types, pos)
        kind, count = (info >> 24)&31, info&65535
        pos += 12
        widths = {1:4,2:0,3:12,4:12*count,5:12*count,6:8*count,7:0,8:0,9:0,
                  10:0,11:0,12:0,13:8*count,14:4,15:12*count,16:0,17:4,18:0,19:12*count}
        width = widths[kind]
        payload = types[pos:pos+width]
        if len(payload) != width:
            raise ValueError("Truncated BTF record")
        if kind == 6:
            for no, value in struct.iter_unpack("<II", payload):
                key = name(no)
                if key in wanted:
                    enums[key] = hex(value)
        if kind == 4 and size == 0x40d0:
            for no, tid, bits in struct.iter_unpack("<III", payload):
                key = name(no)
                if key in selected:
                    bits = bits & 0xffffff if info >> 31 else bits
                    assert bits % 8 == 0
                    fields[key] = hex(bits//8)
        pos += width
    assert enums == {"E_AUDIO_INFO_HDMI_IN":"0x2", "E_AUDIO_INFO_MM_IN":"0x4",
                     "AU_DVB_STANDARD_MAT":"0x1b", "Audio_infoType_Is_Atmos_Stream":"0x56",
                     "Audio_infoType_Mat_Stream_Type":"0xb1",
                     "R2_SHM_PARAM_DOLBY_MAT_HDMI_BYPASS":"0xb4"}
    assert fields == {"eAudioSource":"0x1cc", "g_bNextEnableMatBypass":"0x676",
                      "g_bEnableMatBypass":"0x67e", "g_bNextEnableMatEncode":"0x67b",
                      "g_bSupportMs12MatEncoder":"0x684", "g_bIsEARCDevice":"0x3f5e"}
    report.update(enums=enums, fields=fields, live_identity_verified=False, runtime_validated=False)
    return report

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("module", type=Path)
    args = p.parse_args()
    print(json.dumps(inspect(args.module.read_bytes()), indent=2))
