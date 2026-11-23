#!/usr/bin/env python3
"""Verify a Magisk-patched Android boot image against its stock base (offline, read-only).
Checks: valid boot v3/v4 header; kernel byte-identical to stock (Magisk only touches the
ramdisk); ramdisk differs; Magisk markers present. Exit 0 only if all pass.

Usage: verify_magisk_boot.py <stock_boot.img> <patched_boot.img>
"""
import hashlib, struct, sys

PAGE = 4096

def parse(path):
    d = open(path, "rb").read()
    if d[:8] != b"ANDROID!":
        raise SystemExit(f"{path}: not an Android boot image")
    kern = struct.unpack_from("<I", d, 8)[0]
    rd   = struct.unpack_from("<I", d, 12)[0]
    hv   = struct.unpack_from("<I", d, 40)[0]
    if hv not in (3, 4):
        # v0-2 have different layout; this tool targets the v3/v4 used by this device
        raise SystemExit(f"{path}: header version {hv} not supported by this verifier")
    def pad(n): return (n + PAGE - 1) // PAGE * PAGE
    koff = PAGE
    roff = koff + pad(kern)
    kernel = d[koff:koff+kern]
    ramdisk = d[roff:roff+rd]
    return dict(size=len(d), hv=hv, kern=kern, rd=rd, kernel=kernel, ramdisk=ramdisk, raw=d)

def sha(b): return hashlib.sha256(b).hexdigest()

def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    s = parse(sys.argv[1]); p = parse(sys.argv[2])
    ok = True
    print(f"stock   : size={s['size']} hv={s['hv']} kernel={s['kern']} ramdisk={s['rd']}")
    print(f"patched : size={p['size']} hv={p['hv']} kernel={p['kern']} ramdisk={p['rd']}")

    if sha(s["kernel"]) == sha(p["kernel"]):
        print(f"[PASS] kernel byte-identical ({sha(p['kernel'])[:16]}...) — same firmware kernel")
    else:
        print(f"[FAIL] kernel DIFFERS — stock {sha(s['kernel'])[:16]}... vs patched {sha(p['kernel'])[:16]}...")
        print("       A patched image must keep the stock kernel. Do NOT flash this.")
        ok = False

    if sha(s["ramdisk"]) != sha(p["ramdisk"]):
        print(f"[PASS] ramdisk changed (stock {s['rd']}B -> patched {p['rd']}B)")
    else:
        print("[FAIL] ramdisk identical — Magisk did not patch the ramdisk. Do NOT flash this.")
        ok = False

    markers = sum(p["raw"].lower().count(m) for m in (b"magisk", b"magiskinit"))
    if markers:
        print(f"[PASS] Magisk markers present (count={markers})")
    else:
        print("[WARN] no plaintext 'magisk' marker (ramdisk may be compressed). "
              "Not conclusive; rely on the kernel-identical + ramdisk-changed checks.")

    print("\nRESULT:", "OK to proceed to the flash gate." if ok else "REJECTED — do not flash.")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
