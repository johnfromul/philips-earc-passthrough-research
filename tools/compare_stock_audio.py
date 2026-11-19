#!/usr/bin/env python3
"""Compare local stock images to the previously captured TV audio hashes.
Run as a regular Linux/WSL user, with erofs-utils and e2fsprogs installed.
Nothing is mounted, flashed, patched, or sent to the TV.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

IMAGES = {
    "system": (469409792, "53fd49c3a78a16263a47d9d224ef90cecaf37f0b70ed96f8c3fb508be5bb5a93"),
    "system_ext": (1016545280, "b9593f15beffead734b9f20ee8cdd5bc4735d02c046ac878f22e49e6137f3406"),
    "vendor": (343285760, "90027af82cb035a2b9f8e8ff6f193859cc1849db571d1faf4f037c27437b29ee"),
    "vbmeta": (12288, "b1515c0928302458896604575733ed6052e24759cb079af7e2998a3bc5e208d4"),
}
CLIENT = "dff373698a6eb20316cfa26031529c9c2b5d8edf523174a22668fd0dbc5dd4a9"
EXPECTED = {
    "system_client": CLIENT,
    "tcore_client": CLIENT,
    "vendor_hal": "e47bfd65a6055987fcdfbc9a67166b8024061504d7c5d84e7e88b2b53cf10bad",
    "vendor_policy": "4fbbdd581ec2415d04aab5edfc139a5585af10cac9ed85bbd4ab523dc0518884",
}
MAX_APEX_BYTES = 2 * 1024**3

def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def fs_type(image):
    with image.open("rb") as f:
        f.seek(1024)
        if f.read(4) == bytes.fromhex("e2e1f5e0"):
            return "erofs"
        f.seek(1080)
        if f.read(2) == bytes.fromhex("53ef"):
            return "ext4"
    raise ValueError("Unknown image filesystem: " + image.name)

def extract(image, dest):
    kind = fs_type(image)
    dest.mkdir()
    if kind == "erofs":
        cmd = ["fsck.erofs", "--extract=" + str(dest), str(image)]
    else:
        # debugfs opens read-only by default; never use -w.
        cmd = ["debugfs", "-R", 'rdump / "' + str(dest) + '"', str(image)]
    print("Extracting", image.name, "as", kind, flush=True)
    subprocess.run(cmd, check=True)
    return kind

def walk(root):
    for base, dirs, files in os.walk(root, followlinks=False):
        for name in files:
            p = Path(base) / name
            if not p.is_symlink() and p.is_file():
                yield p

def unzip_member(archive, member, dest):
    with zipfile.ZipFile(archive) as z:
        matches = [i for i in z.infolist() if i.filename == member]
        if len(matches) != 1 or matches[0].file_size > MAX_APEX_BYTES:
            raise ValueError("Missing, duplicate or excessive APEX member: " + member)
        with z.open(matches[0]) as r, dest.open("xb") as w:
            shutil.copyfileobj(r, w, 1024 * 1024)

def audit(images, work, report):
    rows = report["candidates"]
    def record(role, p, location):
        digest = sha(p)
        rows.append({"role": role, "location": location,
                     "bytes": p.stat().st_size, "sha256": digest,
                     "expected_tv_sha256": EXPECTED[role],
                     "matches_tv": digest == EXPECTED[role]})
    for name in ("system", "system_ext", "vendor"):
        root = work / name
        report["filesystems"][name] = extract(images / (name + ".img"), root)
        for p in walk(root):
            rel = p.relative_to(root).as_posix()
            loc = name + ":/" + rel
            if p.name == "libaudioclient.so":
                if name == "system" and rel in ("lib/libaudioclient.so", "system/lib/libaudioclient.so"):
                    record("system_client", p, loc)
                elif "tcore" in rel.lower() and rel.endswith("/lib/libaudioclient.so"):
                    record("tcore_client", p, loc)
            if name == "vendor" and rel == "lib/hw/audio.primary.merak.so":
                record("vendor_hal", p, loc)
            if name == "vendor" and rel == "etc/primary_audio_policy_configuration.xml":
                record("vendor_policy", p, loc)
            if "tcore" in p.name.lower() and p.suffix.lower() in (".apex", ".capex"):
                print("Inspecting", loc, flush=True)
                nested = Path(tempfile.mkdtemp(prefix="tcore_", dir=work))
                try:
                    apex = p
                    if p.suffix.lower() == ".capex":
                        apex = nested / "original.apex"
                        unzip_member(p, "original_apex", apex)
                    payload = nested / "apex_payload.img"
                    unzip_member(apex, "apex_payload.img", payload)
                    apex_root = nested / "payload"
                    extract(payload, apex_root)
                    candidate = apex_root / "lib/libaudioclient.so"
                    if candidate.is_file() and not candidate.is_symlink():
                        record("tcore_client", candidate, loc + "!/lib/libaudioclient.so")
                    else:
                        report["warnings"].append("No ARM32 client in " + loc)
                except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as e:
                    report["warnings"].append(loc + ": " + str(e))
    report["roles"] = {}
    for role in EXPECTED:
        found = [r for r in rows if r["role"] == role]
        report["roles"][role] = ("missing" if not found else
            "match" if all(r["matches_tv"] for r in found) else
            "different_or_ambiguous")
    report["all_four_roles_match"] = all(v == "match" for v in report["roles"].values())

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--images", type=Path, default=Path("private/stock_v670_us_au02/images"))
    p.add_argument("--out", type=Path, default=Path("private/stock_audio_compare.json"))
    args = p.parse_args()
    if os.geteuid() == 0:
        raise SystemExit("Run as a normal WSL/Linux user, not root.")
    for tool in ("fsck.erofs", "debugfs"):
        if not shutil.which(tool):
            raise SystemExit("Missing " + tool + "; install erofs-utils and e2fsprogs.")
    images = args.images.resolve()
    for name, (size, digest) in IMAGES.items():
        image = images / (name + ".img")
        if image.stat().st_size != size or sha(image) != digest:
            raise SystemExit("Stock image hash/size mismatch: " + image.name)
    work = Path(tempfile.mkdtemp(prefix="tcl_stock_", dir=Path.home()))
    print("Linux workspace (retained for local offline analysis):", work, flush=True)
    report = {"stock_build": "G08_4K_US AU02", "installed_build": "G08_4K_GB AU04",
              "input_image_hashes_verified": True, "signature_verified": False,
              "whole_device_compatibility_verified": False, "filesystems": {},
              "candidates": [], "warnings": [], "all_four_roles_match": False}
    try:
        audit(images, work, report)
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        report["warnings"].append(str(e))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Saved:", args.out)
    print("Four file roles match:", report["all_four_roles_match"])
    print("This does NOT establish whole-image compatibility. Do not flash.")
    return 0 if report["all_four_roles_match"] else 2

if __name__ == "__main__":
    raise SystemExit(main())
