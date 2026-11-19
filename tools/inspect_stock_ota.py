#!/usr/bin/env python3
"""Inspect a local stock A/B OTA ZIP. No extraction, patching or device access.
Reads only selected metadata and the bounded CrAU v2 protobuf manifest.
Schema: AOSP system/update_engine/update_metadata.proto.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import zipfile

MAX_MANIFEST = 64 * 1024 * 1024
MAX_TEXT = 64 * 1024

def read_exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise ValueError("Truncated payload header/manifest")
    return data

def varint(buf, pos):
    value = 0
    for shift in range(0, 70, 7):
        if pos >= len(buf):
            raise ValueError("Truncated varint")
        byte = buf[pos]
        pos += 1
        value |= (byte & 127) << shift
        if not byte & 128:
            return value, pos
    raise ValueError("Overlong varint")

def fields(buf):
    pos = 0
    while pos < len(buf):
        tag, pos = varint(buf, pos)
        field, wire = tag >> 3, tag & 7
        if field == 0:
            raise ValueError("Invalid protobuf field")
        if wire == 0:
            value, pos = varint(buf, pos)
        elif wire in (1, 2, 5):
            if wire == 2:
                size, pos = varint(buf, pos)
            else:
                size = 8 if wire == 1 else 4
            if size > len(buf) - pos:
                raise ValueError("Truncated protobuf field")
            value = buf[pos:pos+size]
            pos += size
        else:
            raise ValueError("Unsupported protobuf wire type")
        yield field, wire, value

def inspect_manifest(data):
    partitions = []
    minor = None
    for field, wire, value in fields(data):
        if field == 12 and wire == 0:
            minor = value
        if field != 13 or wire != 2:
            continue
        part = {"name": None, "old_partition_info_present": False}
        for n, w, v in fields(value):
            if n == 1 and w == 2:
                part["name"] = v.decode("utf-8")
            elif n == 6 and w == 2:
                part["old_partition_info_present"] = True
            elif n == 7 and w == 2:
                for pn, pw, pv in fields(v):
                    if pn == 1 and pw == 0:
                        part["new_size"] = pv
                    elif pn == 2 and pw == 2:
                        part["new_sha256"] = pv.hex()
        if not part["name"]:
            raise ValueError("Partition without name")
        partitions.append(part)
    if not partitions:
        raise ValueError("No A/B partition entries found")
    return {"minor_version": minor, "partitions": partitions,
            "old_partition_info_present": any(p["old_partition_info_present"] for p in partitions)}

def inspect(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024*1024), b""):
            h.update(block)
    result = {"filename": path.name, "bytes": path.stat().st_size,
              "sha256": h.hexdigest(), "signature_verified": False}
    with zipfile.ZipFile(path) as z:
        entries = z.infolist()
        result["entries"] = [{"name": e.filename, "bytes": e.file_size} for e in entries]
        for name in ("META-INF/com/android/metadata", "payload_properties.txt"):
            matches = [e for e in entries if e.filename == name]
            if len(matches) > 1:
                raise ValueError("Ambiguous duplicate ZIP entry: "+name)
            if matches and matches[0].file_size <= MAX_TEXT:
                result[name] = z.read(matches[0]).decode("utf-8", errors="replace")
        payload = [e for e in entries if e.filename == "payload.bin"]
        if len(payload) != 1:
            result["payload_status"] = "Missing or ambiguous payload.bin; not parsed"
            return result
        with z.open(payload[0]) as f:
            magic, version, size = struct.unpack(">4sQQ", read_exact(f, 20))
            if magic != b"CrAU" or version != 2:
                raise ValueError("Expected Android CrAU payload version 2")
            sig_size, = struct.unpack(">I", read_exact(f, 4))
            if size > MAX_MANIFEST or size + sig_size + 24 > payload[0].file_size:
                raise ValueError("Invalid or excessive manifest size")
            result["payload"] = {"version": version, "manifest_bytes": size,
                                 "metadata_signature_bytes": sig_size,
                                 **inspect_manifest(read_exact(f, size))}
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("stock_zip", type=pathlib.Path)
    p.add_argument("--out", type=pathlib.Path, default=pathlib.Path("private/stock_ota_inventory.json"))
    args = p.parse_args()
    result = inspect(args.stock_zip)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print("Saved:", args.out)
    print("Metadata only; signature, model compatibility and recovery NOT validated.")

if __name__ == "__main__":
    main()
