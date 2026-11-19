#!/usr/bin/env python3
"""Offline, read-only inventory of audio-related stock DT nodes. Python 3.8+."""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STOCK = "3d2fdf5363d86061f7b30855458e673a39cada336ec0359a12a11533362793e9"
FDT = b"\xd0\x0d\xfe\xed"
TABLE = b"\xd7\xb7\xab\x1e"
AUDIO = re.compile(r"audio|aud[_-]|tvsnd|sound|earc|hdmi|aloop|loopback|offload|dsp|spdif|i2s|aif|dai-link", re.I)
PROP = re.compile(r"^(compatible|status|model|name|target|target-path|phandle|linux,phandle|reg|ranges|#.*cells|.*names|.*audio.*|.*sound.*|.*dai.*|.*clock.*|.*clk.*|.*parent.*|reg[-_]offset|.*interrupt.*|.*memory-region.*|.*mixing.*|.*loopback.*|.*endpoint.*|.*port.*|.*dma.*|.*format.*|.*rate.*|.*channel.*|.*route.*|.*mux.*|.*source.*|.*sink.*)$", re.I)
MAX = 256 * 1024 * 1024


def align(n, a):
    return (n + a - 1) // a * a


def digest(data):
    return hashlib.sha256(data).hexdigest()


def image(root, manifest, name):
    entries = [p for p in manifest["partitions"] if p["name"] == name]
    if len(entries) != 1:
        raise ValueError("Missing or ambiguous partition: " + name)
    p = entries[0]
    path = root / (name + ".img")
    if path.stat().st_size > MAX:
        raise ValueError("Image exceeds audit limit: " + name)
    data = path.read_bytes()
    if not p["matches"] or len(data) != p["expected_bytes"] or digest(data) != p["expected_sha256"].lower():
        raise ValueError("Image hash/size mismatch: " + name)
    return data


def vendor_region(data):
    if len(data) < 2112 or data[:8] != b"VNDRBOOT":
        raise ValueError("Unsupported vendor_boot header")
    version, page = struct.unpack_from("<II", data, 8)
    ramdisk = struct.unpack_from("<I", data, 24)[0]
    header, size = struct.unpack_from("<II", data, 2096)
    if version not in (3, 4) or page < 512 or page > 65536 or page & (page-1):
        raise ValueError("Unsupported vendor_boot layout")
    if header < (2128 if version == 4 else 2112) or header > len(data):
        raise ValueError("Invalid vendor_boot header size")
    offset = align(header, page) + align(ramdisk, page)
    if not size or offset + size > len(data):
        raise ValueError("Missing or truncated vendor DTB area")
    return data[offset:offset+size], {"header_version": version, "dtb_offset": offset, "dtb_bytes": size}


def parse_fdt(data):
    if len(data) < 40 or data[:4] != FDT:
        raise ValueError("Invalid FDT magic")
    _, total, off_struct, off_strings, off_reserve, version, last, _, strings_size, struct_size = struct.unpack_from(">10I", data)
    if version < 17 or last > 17 or not (40 <= total <= len(data)):
        raise ValueError("Unsupported FDT header")
    if not (40 <= off_struct <= total and 40 <= off_strings <= total and 40 <= off_reserve < total):
        raise ValueError("FDT block out of bounds")
    if off_struct + struct_size > total or off_strings + strings_size > total:
        raise ValueError("Truncated FDT block")
    st = data[off_struct:off_struct+struct_size]
    names = data[off_strings:off_strings+strings_size]
    pos = 0
    stack = []
    nodes = {}
    while pos + 4 <= len(st):
        token = struct.unpack_from(">I", st, pos)[0]
        pos += 4
        if token == 1:
            end = st.index(b"\0", pos)
            name = st[pos:end].decode("ascii")
            if "/" in name or (not stack and name != "") or (stack and not name):
                raise ValueError("Invalid node name")
            stack.append(name)
            path = "/".join(stack) or "/"
            if path in nodes:
                raise ValueError("Duplicate node: " + path)
            nodes[path] = {}
            pos = align(end+1, 4)
        elif token == 2:
            if not stack:
                raise ValueError("Unbalanced FDT nodes")
            stack.pop()
        elif token == 3:
            if not stack or pos + 8 > len(st):
                raise ValueError("Invalid FDT property")
            size, noff = struct.unpack_from(">II", st, pos)
            pos += 8
            if pos + size > len(st) or noff >= len(names):
                raise ValueError("Truncated property")
            key = names[noff:names.index(b"\0", noff)].decode("ascii")
            node = nodes["/".join(stack) or "/"]
            if key in node:
                raise ValueError("Duplicate property")
            node[key] = st[pos:pos+size]
            pos = align(pos+size, 4)
        elif token == 4:
            continue
        elif token == 9:
            if stack or "/" not in nodes:
                raise ValueError("Unbalanced FDT end")
            return total, nodes
        else:
            raise ValueError("Unknown FDT token")
    raise ValueError("Missing FDT end")


def strings(value):
    if value and value.endswith(b"\0"):
        items = value[:-1].split(b"\0")
        if all(s and all(32 <= c < 127 for c in s) for s in items):
            return [s.decode("ascii") for s in items]
    return None


def describe(value):
    if not value:
        return {"boolean": True}
    # Preserve bytes even when a numeric cell happens to look like text.
    result = {"hex": value.hex()}
    s = strings(value)
    if s is not None:
        result["strings"] = s
    if len(value) % 4 == 0:
        result["cells_be"] = list(struct.unpack(">" + "I"*(len(value)//4), value))
    return result


def summarize(nodes):
    seeds = set()
    for path, props in nodes.items():
        if path.startswith(("/__symbols__", "/__fixups__", "/__local_fixups__")):
            continue
        values = (strings(props.get("compatible", b"")) or []) + (strings(props.get("target-path", b"")) or [])
        if path != "/" and (AUDIO.search(path.rsplit("/", 1)[-1]) or any(AUDIO.search(s) for s in values)):
            seeds.add(path)
    chosen = {p for p in nodes if any(p == s or p.startswith(s+"/") for s in seeds)}
    # Include overlay fragment metadata for matched descendants.
    for p in list(chosen):
        first = "/" + p.split("/")[1]
        if first.startswith("/fragment@"):
            chosen.add(first)
    # Resolve only single-cell direct phandle properties (not arbitrary cells).
    handles = {}
    for p, props in nodes.items():
        for k in ("phandle", "linux,phandle"):
            v = props.get(k, b"")
            if len(v) == 4:
                handles[struct.unpack(">I", v)[0]] = p
    rows = []
    for p in sorted(chosen):
        props = nodes[p]
        row = {"path": p, "properties": {k: describe(v) for k,v in props.items() if PROP.match(k)}}
        refs = {}
        for k in ("remote-endpoint", "memory-region", "sound-dai", "target"):
            v = props.get(k, b"")
            if len(v) == 4:
                h = struct.unpack(">I", v)[0]
                refs[k] = handles.get(h)
        if refs:
            row["single_cell_refs_in_same_blob"] = refs
        rows.append(row)
    # Overlay target placeholders are external references, not missing hardware.
    # Report fixups on selected nodes only; do not apply overlays.
    fixups = {}
    for symbol, value in nodes.get("/__fixups__", {}).items():
        for item in strings(value) or []:
            fields = item.rsplit(":", 2)
            if len(fields) == 3 and fields[0] in chosen:
                fixups.setdefault(symbol, []).append(item)
    root = nodes["/"]
    return {"node_count": len(nodes), "root_compatible": strings(root.get("compatible", b"")),
            "root_model": strings(root.get("model", b"")), "audio_nodes": rows,
            "selected_external_fixups": fixups}


def record(data, offset, meta):
    total, nodes = parse_fdt(data)
    return dict(meta, offset=offset, fdt_bytes=total, fdt_sha256=digest(data[:total]), **summarize(nodes))


def concat(region):
    rows = []
    pos = 0
    while pos < len(region):
        if region[pos] == 0:
            pos += 1
            continue
        if region[pos:pos+4] != FDT:
            raise ValueError("Unexpected data in concatenated DTB area at " + str(pos))
        row = record(region[pos:], pos, {})
        rows.append(row)
        pos += row["fdt_bytes"]
    if not rows:
        raise ValueError("No DTBs in region")
    return rows


def vendor_candidates(region):
    """Try strict concatenation first; otherwise inventory validated FDTs in
    the declared DTB region without interpreting nonstandard wrapper bytes."""
    meta = {"region_prefix_hex": region[:32].hex()}
    try:
        return dict(meta, mode="strict_concat", entries=concat(region))
    except (ValueError, struct.error) as exc:
        meta["strict_error"] = str(exc)
    if region.startswith(TABLE):
        try:
            table = dtbo(region)
            return dict(meta, mode="android_dt_table", **table)
        except (ValueError, struct.error) as exc:
            meta["table_error"] = str(exc)
    rows = []
    rejected = 0
    attempts = 0
    pos = 0
    while True:
        hit = region.find(FDT, pos)
        if hit < 0:
            break
        attempts += 1
        if attempts > 4096:
            raise ValueError("Excessive candidate count in vendor DTB region")
        pos = hit + len(FDT)
        try:
            if hit + 40 > len(region):
                raise ValueError("Truncated candidate header")
            size = struct.unpack_from(">I", region, hit + 4)[0]
            if not 40 <= size <= len(region) - hit:
                raise ValueError("Candidate size outside declared region")
            row = record(region[hit:hit+size], hit, {})
            rows.append(row)
            pos = hit + row["fdt_bytes"]
        except (ValueError, struct.error):
            rejected += 1
    return dict(meta, mode="validated_fdt_scan", entries=rows, rejected_candidates=rejected,
                warning="Wrapper not decoded; candidate DTBs are not proof of the active base tree"
                if rows else "No valid uncompressed FDT found in declared region")


def dtbo(data):
    if data[:4] == FDT:
        return {"format": "raw FDT", "entries": concat(data)}
    if len(data) < 32 or data[:4] != TABLE:
        raise ValueError("Unsupported DTBO table/header")
    _, total, header, entry_size, count, entries, page, version = struct.unpack_from(">8I", data)
    if version not in (0,1) or not (32 <= header <= entries <= total <= len(data)) or entry_size < 32 or count > 4096 or entries + count*entry_size > total:
        raise ValueError("Invalid DTBO table layout")
    rows = []
    for i in range(count):
        size, offset, ident, rev, flags, _, _, _ = struct.unpack_from(">8I", data, entries+i*entry_size)
        if not size or offset < entries+count*entry_size or offset+size > total:
            raise ValueError("DTBO entry outside table")
        meta = {"index": i, "id": ident, "rev": rev, "entry_bytes": size}
        if version == 1 and flags & 0xf:
            rows.append(dict(meta, offset=offset, warning="Compressed DTBO not parsed", flags=flags))
        else:
            rows.append(record(data[offset:offset+size], offset, meta))
    return {"format": "Android DT table", "version": version, "page_size": page, "entries": rows}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--images", type=Path, default=ROOT/"private/stock_v670_us_au02/boot_audit_images")
    p.add_argument("--manifest", type=Path, default=ROOT/"private/stock_boot_audit_verified.json")
    p.add_argument("--out", type=Path, default=ROOT/"private/stock_audio_dt_topology_v2.json")
    a = p.parse_args()
    manifest = json.loads(a.manifest.read_text(encoding="utf-8-sig"))
    if manifest.get("stock_sha256") != STOCK:
        raise SystemExit("Unexpected stock manifest")
    report = {"source": "V670 US AU02 stock", "live_tree_verified": False, "overlays_applied": False, "warnings": []}
    for name in ("vendor_boot", "dtbo"):
        data = image(a.images, manifest, name)
        try:
            if name == "vendor_boot":
                region, meta = vendor_region(data)
                report[name] = dict(meta, **vendor_candidates(region))
                if report[name].get("warning"):
                    report["warnings"].append(name+": "+report[name]["warning"])
            else:
                report[name] = dtbo(data)
        except (ValueError, struct.error) as e:
            report["warnings"].append(name+": "+str(e))
        report[name+"_sha256"] = digest(data)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print("Saved:", a.out)
    print("Warnings:", report["warnings"])
    print("Stock base/overlay candidates only. No device access or modification.")

if __name__ == "__main__":
    main()
