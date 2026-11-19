#!/usr/bin/env python3
"""Collect selected stock audio configuration candidates, offline only."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile

EXPECTED = {
    "firmware/dsp.bin": 53809,
    "firmware/DOLBY.bin": 18944,
    "firmware/md5.txt": 384,
    "firmware/audio/audio_init.ini": 14880,
    "firmware/audio/audio_init_e3.ini": 11688,
    "firmware/audio/audio_init_mt5897.ini": 11864,
    "firmware/audio/audio_pre_init.ini": 175,
    "firmware/audio/audio_pre_init_e3.ini": 3556,
    "firmware/audio/audio_pre_init_mt5897.ini": 3423,
    "firmware/audio/earc.ini": 148,
    "firmware/audio/earc_mt5897.ini": 148,
    "etc/dolby/audio/device_descriptors/Dolby_TV.json": 53935,
    "etc/dolby/audio/device_descriptors/Dolby_Skyline.json": 18286,
}

def collect(root, out):
    if not root.is_dir() or root.is_symlink():
        raise ValueError("Expected a regular extracted vendor directory.")
    root = root.resolve()
    rows = []
    payloads = {}
    for name, size in EXPECTED.items():
        p = root / name
        if any(q.is_symlink() for q in [p, *p.parents] if q != root and root in q.parents):
            raise ValueError("Symlink rejected: " + name)
        if not p.is_file() or not p.resolve().is_relative_to(root):
            raise ValueError("File missing or outside vendor tree: " + name)
        if p.stat().st_size != size:
            raise ValueError("Unexpected size: " + name)
        data = p.read_bytes()
        if len(data) != size:
            raise ValueError("File changed while reading: " + name)
        payloads[name] = data
        rows.append({"path": name, "bytes": len(data),
                     "sha256": hashlib.sha256(data).hexdigest()})
    manifest = {"scope": "selected stock V670 vendor candidates",
                "workspace": root.parent.name, "runtime_validated": False,
                "files": rows}
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        for name, data in payloads.items():
            z.writestr(name, data)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("xb") as f:
        f.write(buffer.getvalue())
    print("Saved:", out, "|", len(rows), "files")
    print("Private archive: do not commit or redistribute proprietary files.")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor", type=Path,
                        default=Path.home() / "tcl_stock_040p83wi" / "vendor")
    parser.add_argument("--out", type=Path,
                        default=Path("private/dsp_candidates_stock.zip"))
    args = parser.parse_args()
    try:
        collect(args.vendor, args.out)
    except (OSError, ValueError) as exc:
        parser.exit(1, "Collection failed: " + str(exc) + "\n")

if __name__ == "__main__":
    main()
