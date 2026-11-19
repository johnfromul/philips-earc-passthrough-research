#!/usr/bin/env python3
"""Inspect retained compare_stock_audio workspace; no extraction or device writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import zipfile

EXPECTED = "dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--out", type=Path, default=Path("private/tcore_layout.json"))
    args = parser.parse_args()
    work = args.workspace
    if work is None:
        candidates = [p for p in Path.home().glob("tcl_stock_*")
                      if p.is_dir() and not p.is_symlink() and (p / "system_ext").is_dir()]
        if not candidates:
            raise SystemExit("No retained tcl_stock_* workspace; specify --workspace.")
        work = max(candidates, key=lambda p: p.stat().st_mtime)
    if not work.is_dir() or work.is_symlink():
        raise SystemExit("Invalid workspace")
    def inventory(root):
        result = []
        for base, dirs, files in os.walk(root, followlinks=False):
            for name in sorted(dirs + files):
                p = Path(base) / name
                if name != "libaudioclient.so" and not (
                    "tcore" in name.lower() and name.endswith((".apex", ".capex"))):
                    continue
                s = p.lstat()
                row = {"path": p.relative_to(work).as_posix()}
                if stat.S_ISLNK(s.st_mode):
                    row.update(kind="symlink", target=os.readlink(p))
                elif stat.S_ISREG(s.st_mode):
                    row.update(kind="file", bytes=s.st_size)
                    if name == "libaudioclient.so":
                        row["sha256"] = hashlib.sha256(p.read_bytes()).hexdigest()
                        row["matches_tv"] = row["sha256"] == EXPECTED
                    elif name.endswith((".apex", ".capex")):
                        with zipfile.ZipFile(p) as z:
                            row["archive_members"] = [{"name": i.filename, "bytes": i.file_size}
                                                      for i in z.infolist()]
                else:
                    row["kind"] = "other"
                result.append(row)
        return result
    report = {"workspace_name": work.name, "entries": inventory(work),
              "note": "No symlink followed; actual active APEX source is a separate device-side question."}
    for root in sorted(work.glob("tcore_*/payload")):
        if root.is_symlink():
            continue
        for lib in (root / "lib", root / "lib64"):
            if lib.is_symlink():
                report.setdefault("lib_directory_symlinks", []).append(
                    {"path": lib.relative_to(work).as_posix(), "target": os.readlink(lib)})
            elif lib.is_dir():
                report.setdefault("lib_directory_entries", {})[lib.relative_to(work).as_posix()] = sorted(p.name for p in lib.iterdir())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Saved:", args.out)
    print("Inspect the active APEX separately using adb shell cat /apex/apex-info-list.xml.")

if __name__ == "__main__":
    main()
