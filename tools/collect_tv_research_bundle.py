#!/usr/bin/env python3
"""One ZIP: existing read-only TCL ADB/stock collector + passive ESP32 UART evidence.

Python 3.9+. No root, boot writes, UART TX, reboots, unlocking or flashing.
This wrapper never archives ESP Wi-Fi credentials, tokens or compiled firmware.
"""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TOOLKIT = ROOT.parent / "tcl-tv-toolkit"
MAX_UART_BYTES = 8 * 1024 * 1024
MAX_SECONDS = 600


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path, obj):
    path.write_text(json.dumps(obj, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")


def run_logged(name, cmd, folder, timeout=1800):
    out = folder / (name + ".stdout.txt")
    err = folder / (name + ".stderr.txt")
    try:
        with out.open("wb") as stdout, err.open("wb") as stderr:
            r = subprocess.run(cmd, stdout=stdout, stderr=stderr, timeout=timeout,
                               stdin=subprocess.DEVNULL)
        return {"status": "ok" if r.returncode == 0 else "failed",
                "returncode": r.returncode, "command": name}
    except subprocess.TimeoutExpired:
        return {"status": "timeout", "command": name}
    except OSError as exc:
        return {"status": "unavailable", "command": name, "error": str(exc)}


def read_passively(cmd, folder, seconds):
    """Capture a bounded, read-only RX stream; terminate only the local reader."""
    raw = folder / "uart_rx.raw"
    stdout_path = folder / "uart_rx.stdout.bin"
    stderr_path = folder / "uart_rx.stderr.txt"
    reader = None
    start = time.monotonic()
    try:
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            reader = subprocess.Popen(cmd + ["read", "--follow", "--save", str(raw)],
                                      stdout=stdout, stderr=stderr, stdin=subprocess.DEVNULL)
            while time.monotonic() - start < seconds:
                if reader.poll() is not None:
                    break
                if raw.is_file() and raw.stat().st_size >= MAX_UART_BYTES:
                    break
                time.sleep(0.25)
            if reader.poll() is None:
                reader.terminate()
                try:
                    reader.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    reader.kill()
                    reader.wait(timeout=5)
        # Raw bytes may contain invalid UTF-8. Keep originals and a bounded human preview.
        if raw.is_file():
            with raw.open("rb") as inp:
                preview = inp.read(512 * 1024)
            (folder / "uart_rx_preview.txt").write_text(
                preview.decode("utf-8", "replace"), encoding="utf-8")
            return {"status":"captured","bytes":raw.stat().st_size,
                    "sha256":sha256(raw),
                    "truncated_preview":raw.stat().st_size>len(preview),
                    "reader_exit":reader.returncode}
        return {"status":"no_data","reader_exit":reader.returncode}
    except (OSError, ValueError) as exc:
        return {"status":"failed","error":str(exc)}
    finally:
        if reader is not None and reader.poll() is None:
            reader.kill()
            reader.wait(timeout=5)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--label", required=True, help="C655-stock, C655-uart, C9K-stock etc.")
    p.add_argument("--serial", help="Select ADB target if more than one is connected")
    p.add_argument("--adb", default="adb", help="ADB binary name/path")
    p.add_argument("--reports-only", action="store_true", help="Do not pull vendor binaries")
    p.add_argument("--no-logs", action="store_true", help="Skip existing audio logcat")
    p.add_argument("--include-packages", action="store_true", help="Include installed package paths")
    p.add_argument("--skip-adb", action="store_true", help="UART-only hardware bring-up")
    p.add_argument("--ota", type=Path, help="Optional matching stock OTA: manifest-only inventory")
    p.add_argument("--images", type=Path, help="Optional already-extracted matching stock images")
    p.add_argument("--esp-toolkit", type=Path, default=DEFAULT_TOOLKIT,
                   help="Checkout of lukasa1993/tcl-tv-toolkit")
    p.add_argument("--no-esp", action="store_true", help="Only ADB/firmware collection")
    p.add_argument("--esp-host", help="ESP hostname or IP; otherwise toolkit default .local")
    p.add_argument("--uart-seconds", type=int, default=0,
                   help="Passive RX during manual TV boot, 1–600 s; default 0 (off)")
    a = p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", a.label):
        p.error("Use a short ASCII label with letters, digits, _ or -")
    if not 0 <= a.uart_seconds <= MAX_SECONDS:
        p.error("--uart-seconds must be 0–600")
    if a.no_esp and a.uart_seconds:
        p.error("--uart-seconds requires ESP enabled")
    if a.skip_adb and (a.ota or a.images):
        p.error("For stock-only inspection call tools/collect_tv_audio_audit.py --offline-only")
    if a.images and not a.ota:
        p.error("--images requires a matching --ota manifest")
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    dest = ROOT / "private" / "research_bundles" / (a.label + "_" + stamp)
    dest.mkdir(parents=True, exist_ok=False)
    manifest = {
        "label":a.label, "utc":stamp, "read_only_tv":True,
        "uart_transmission":False, "device_root_required":False,
        "ota_signature_verified":False, "parts":{},
        "limits":{"max_uart_bytes":MAX_UART_BYTES,"max_uart_seconds":MAX_SECONDS},
        "privacy":"Contains possibly sensitive serials, logs, app paths and proprietary binaries; inspect before sharing."
    }
    try:
        if not a.skip_adb:
            collector = ROOT / "tools" / "collect_tv_audio_audit.py"
            cmd = [sys.executable, str(collector), "--label", a.label,
                   "--adb", a.adb]
            if a.serial:
                cmd += ["--serial", a.serial]
            if a.reports_only:
                cmd.append("--reports-only")
            if not a.no_logs:
                cmd.append("--logs")
            if a.include_packages:
                cmd.append("--privacy-inventory")
            if a.ota:
                cmd += ["--ota", str(a.ota)]
            if a.images:
                cmd += ["--images", str(a.images)]
            print("Collecting ADB/stock evidence (read-only)...", flush=True)
            result = run_logged("adb_audit", cmd, dest)
            # The established collector prints an absolute path after successfully
            # producing the archive, including for a partial capture.
            console = (dest / "adb_audit.stdout.txt").read_text(encoding="utf-8",errors="replace")
            archive_match = re.search(r"(?m)^Saved private evidence:\s*(.+\.zip)\s*$", console)
            if archive_match:
                source = Path(archive_match.group(1).strip()).resolve()
                allowed = (ROOT / "private" / "device_audits").resolve()
                if source.is_file() and source.parent == allowed:
                    target = dest / "adb_audit.zip"
                    # Avoid writing personal/vendor data outside the gitignored tree.
                    import shutil
                    shutil.copyfile(source, target)
                    result["archive_bytes"] = target.stat().st_size
                    result["sha256"] = sha256(target)
                else:
                    result["archive_error"] = "Collector output outside expected private/device_audits directory"
            else:
                result["archive_error"] = "No generated ZIP in collector output (see logs)"
            manifest["parts"]["adb_audit"] = result
        if not a.no_esp:
            bridge = a.esp_toolkit / "esp32" / "bridge.py"
            if bridge.is_file():
                base = [sys.executable, str(bridge)]
                if a.esp_host:
                    base += ["--host", a.esp_host]
                for cmd in ("status", "automation"):
                    # No TX, no disarm, no loopback or ESP mutation.
                    manifest["parts"]["esp_"+cmd] = run_logged("esp_"+cmd,base+[cmd],dest,30)
                if a.uart_seconds:
                    if manifest["parts"]["esp_status"]["status"]!="ok":
                        manifest["parts"]["uart"]={"status":"skipped","reason":"ESP status failed"}
                    else:
                        print("UART receive capture starting. TV wiring must be verified.")
                        print("Power on the TV manually while this receive-only capture is running.",flush=True)
                        manifest["parts"]["uart"]=read_passively(base,dest,a.uart_seconds)
            else:
                manifest["parts"]["esp"]={
                    "status":"unavailable",
                    "detail":"No esp32/bridge.py at supplied toolkit checkout; no ESP actions taken."}
        write_json(dest/"manifest.json",manifest)
    finally:
        if not (dest/"manifest.json").is_file():
            manifest["parts"]["bundle"]={"status":"partial_exception"}
            write_json(dest/"manifest.json",manifest)
        zip_path = dest.with_suffix(".zip")
        with zipfile.ZipFile(zip_path,"x",zipfile.ZIP_DEFLATED,compresslevel=6,
                             allowZip64=True) as archive:
            for source in sorted(dest.rglob("*")):
                if source.is_file() and not source.is_symlink():
                    archive.write(source,source.relative_to(dest).as_posix())
        print("Bundle:",zip_path,flush=True)
        print("Includes errors/missing data as evidence. Review before sharing.",flush=True)
    return 0 if all(v.get("status") in ("ok","captured","no_data")
                    for v in manifest["parts"].values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
