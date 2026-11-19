#!/usr/bin/env python3
"""List potential audio DSP firmware/config files in a retained stock WSL workspace.

Read-only on stock files. No ADB, mounts, firmware writes or executable invocation.
"""
import argparse
import json
import os
from pathlib import Path
import stat

def inventory(root):
    rows = []
    warnings = []
    for base, dirs, files in os.walk(root, followlinks=False,
                                    onerror=lambda exc: warnings.append(str(exc))):
        dirs[:] = sorted(d for d in dirs if not (Path(base) / d).is_symlink())
        for name in sorted(files):
            p = Path(base) / name
            rel = p.relative_to(root)
            lower = name.lower()
            reasons = []
            if "firmware" in (part.lower() for part in rel.parts[:-1]):
                reasons.append("firmware directory")
            if any(key in lower for key in ("dsp", "dconfig", "sconfig", "dolby", "flex", "ms12", "adsp", "audiodsp")):
                reasons.append("audio-related filename")
            if "audio" in lower and lower.endswith((".bin", ".fw", ".elf", ".dat", ".ini", ".cfg")):
                reasons.append("audio binary/config")
            if not reasons:
                continue
            try:
                st = p.lstat()
                if not stat.S_ISREG(st.st_mode):
                    continue
                rows.append({"path": rel.as_posix(), "bytes": st.st_size, "reasons": reasons})
            except OSError as exc:
                warnings.append(str(exc))
    return {"workspace": root.parent.name, "files": rows, "warnings": warnings}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor", type=Path, action="append",
                        help="Explicit extracted vendor directory; repeat if necessary")
    parser.add_argument("--out", type=Path, default=Path("private/dsp_inventory.json"))
    args = parser.parse_args()
    roots = args.vendor or sorted(Path.home().glob("tcl_stock_*/vendor"))
    roots = [p for p in roots if p.is_dir() and not p.is_symlink()]
    if not roots:
        parser.error("No extracted vendor tree found. Supply --vendor PATH; do not re-extract yet.")
    report = {"scope": "retained stock vendor trees; candidate filenames only",
              "runtime_validated": False, "workspaces": [inventory(p) for p in roots]}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Saved:", args.out)
    print("Candidates:", sum(len(w["files"]) for w in report["workspaces"]))

if __name__ == "__main__":
    main()
