#!/usr/bin/env python3
"""Collect selected driver files from a verified stock partition, offline only."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile
from compare_stock_audio import sha, extract, walk

IMAGE = Path("private/stock_v670_us_au02/drivers_images/vendor_dlkm.img")
SIZE = 89944064
HASH = "02cdb6ac2e318eb1f96b8679d1272b9a2d3e3c984056b5bb8438c5d5d9a0c6a1"
EXPECTED = {
    "mtk-earc.ko": 210200,
    "mtk-tvsnd-machine.ko": 350400,
    "mtk-tvsnd-platform.ko": 662992,
    "mtk-tvsnd-soc.ko": 3071872,
    "mtk-tvsnd-dsp-loader.ko": 12648,
    "audio-public-if.ko": 12952,
}

def collect(root, output):
    candidates = {}
    for path in walk(root):
        if path.name in EXPECTED:
            if path.name in candidates:
                raise ValueError("Ambiguous module path: " + path.name)
            candidates[path.name] = path
    if set(candidates) != set(EXPECTED):
        raise ValueError("Missing modules: " + ", ".join(sorted(set(EXPECTED)-set(candidates))))
    if output.exists():
        raise ValueError("Output already exists: " + str(output))
    rows = []
    for name, path in sorted(candidates.items()):
        with path.open("rb") as f:
            if f.read(4) != b"\x7fELF":
                raise ValueError("Not an ELF module: " + name)
        size = path.stat().st_size
        info = subprocess.run(["readelf", "-h", "-p", ".modinfo", str(path)],
                              capture_output=True, text=True, errors="replace", check=False)
        vermagic = re.search(r"vermagic=([^\r\n]+)", info.stdout)
        rows.append({"name": name, "source_path": path.relative_to(root).as_posix(),
                     "bytes": size, "sha256": sha(path), "tv_listed_bytes": EXPECTED[name],
                     "size_matches_tv_listing": size == EXPECTED[name],
                     "vermagic": vermagic.group(1).strip() if vermagic else None,
                     "readelf_exit": info.returncode})
    report = {"source": "V670 G08_4K_US AU02 OTA vendor_dlkm",
              "installed": "V670 G08_4K_GB AU04; kernel 5.15.192-android14-11",
              "partition_sha256": HASH, "partition_bytes": SIZE,
              "partition_verified": True, "live_module_hashes_verified": False,
              "modules": rows}
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(report, indent=2) + "\n")
        for name, path in sorted(candidates.items()):
            z.write(path, "modules/" + name)
    print("Saved:", output)
    print("All sizes match TV listing:", all(r["size_matches_tv_listing"] for r in rows))
    print("Stock image verified; live module byte identity NOT verified. No device writes.")

def main():
    if os.geteuid() == 0:
        raise SystemExit("Run as a normal WSL/Linux user, not root.")
    for tool in ("fsck.erofs", "debugfs", "readelf"):
        if not shutil.which(tool):
            raise SystemExit("Missing " + tool + "; install erofs-utils e2fsprogs binutils.")
    if IMAGE.stat().st_size != SIZE or sha(IMAGE) != HASH:
        raise SystemExit("Unexpected vendor_dlkm image; refusing extraction")
    output = Path("private/earc_drivers_stock.zip").resolve()
    if output.exists():
        raise SystemExit("Output exists; preserve or rename before rerunning.")
    # Keep POSIX extraction on the WSL Linux filesystem; no symlinks followed.
    with tempfile.TemporaryDirectory(prefix="tcl_drivers_", dir=Path.home()) as tmp:
        root = Path(tmp) / "vendor_dlkm"
        extract(IMAGE.resolve(), root)
        collect(root, output)

if __name__ == "__main__":
    main()
