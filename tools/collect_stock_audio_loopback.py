#!/usr/bin/env python3
"""Collect loopback modules and module load lists from verified stock vendor_dlkm."""
import json
import os
from pathlib import Path
import shutil
import tempfile
import zipfile

from collect_stock_earc_drivers import IMAGE, SIZE, HASH
from compare_stock_audio import sha, extract, walk

WANTED = {"mtk_aloop", "snd_aloop"}
MAX_LIST = 1024 * 1024


def main():
    if os.geteuid() == 0:
        raise SystemExit("Run as a regular WSL/Linux user.")
    for tool in ("debugfs", "fsck.erofs"):
        if not shutil.which(tool):
            raise SystemExit("Missing " + tool)
    if IMAGE.stat().st_size != SIZE or sha(IMAGE) != HASH:
        raise SystemExit("Unexpected stock image")
    output = Path("private/audio_loopback_stock.zip").resolve()
    if output.exists():
        raise SystemExit("Output exists; preserve or rename it first.")
    with tempfile.TemporaryDirectory(prefix="tcl_loopback_", dir=Path.home()) as tmp:
        root = Path(tmp) / "vendor_dlkm"
        kind = extract(IMAGE.resolve(), root)
        rows = []
        found = set()
        with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as z:
            for p in sorted(walk(root)):
                name = p.name
                module = name[:-3].replace("-", "_") if name.endswith(".ko") else ""
                if module in WANTED:
                    with p.open("rb") as f:
                        if f.read(4) != b"\x7fELF":
                            raise ValueError("Not ELF: " + str(p))
                    found.add(module)
                    role = "loopback_module"
                elif (name == "modules.load" or name.startswith("modules.load.")
                        or name == "modules.softdep" or name == "modules.dep"):
                    if p.stat().st_size > MAX_LIST:
                        raise ValueError("Unexpectedly large module list: " + str(p))
                    role = "module_list"
                else:
                    continue
                rel = p.relative_to(root).as_posix()
                rows.append({"path": rel, "role": role, "bytes": p.stat().st_size, "sha256": sha(p)})
                z.write(p, "vendor_dlkm/" + rel)
            z.writestr("manifest.json", json.dumps({
                "partition_sha256": HASH, "partition_verified": True,
                "filesystem": kind, "missing_loopback_modules": sorted(WANTED - found),
                "live_module_identity_verified": False, "files": rows
            }, indent=2) + "\n")
    print("Saved:", output)
    print("Missing from vendor_dlkm:", sorted(WANTED - found))
    print("Offline collection only; no TV changes.")

if __name__ == "__main__":
    main()
