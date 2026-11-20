#!/usr/bin/env python3
"""Read-only, model-independent TCL audio research collector (Python 3.9+)."""
import argparse
import datetime as dt
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MAX_FILE = 64 * 1024**2
MAX_TOTAL = 768 * 1024**2
MAX_IMAGE = 256 * 1024**2
PARTS = ("boot", "init_boot", "vendor_boot", "dtbo", "mboot", "vbmeta",
         "system", "vendor", "odm", "system_ext", "product", "vendor_dlkm", "system_dlkm", "odm_dlkm")
PROPS = """ro.product.model ro.product.name ro.product.device ro.product.board
ro.product.vendor.model ro.product.vendor.device ro.product.manufacturer
ro.hardware ro.hardware.version_id ro.board.platform ro.soc.model ro.soc.manufacturer
ro.build.fingerprint ro.vendor.build.fingerprint ro.build.display.id ro.build.id
ro.build.version.release ro.build.version.sdk ro.build.version.incremental
ro.build.version.security_patch ro.product.cpu.abilist ro.product.cpu.abilist64
ro.boot.bootloader ro.boot.verifiedbootstate ro.boot.vbmeta.device_state
ro.boot.vbmeta.digest ro.boot.veritymode ro.boot.flash.locked ro.oem_unlock_supported
sys.oem_unlock_allowed ro.boot.slot_suffix ro.boot.dtbo_idx ro.build.ab_update
ro.build.dynamic_partitions ro.virtual_ab.enabled ro.product.ab_ota_partitions
vendor.mtk.mkv.support.truehd vendor.hal.audio.disable.parser""".split()

LIB = re.compile(r"audio|dolby|ms12|lapaf|truehd|(^|_)mat($|_)|earc|spdif|hdmi|codec2|c2store|mtkadwrapper|extractor|mediaparser|tcore|dts|tinyalsa|aml.*audio", re.I)
MOD = re.compile(r"snd|audio|earc|aloop|tvsnd|spdif|dsp|hdmirx|hdmi_rx|channel.*mux", re.I)
CONF = re.compile(r"audio|dolby|ms12|earc|spdif|media_codecs|media_profiles|mixer_paths|dsp|codec2|tcore|hdmi", re.I)
SAFE_PATH = re.compile(r"^/(?:system|system_ext|vendor|odm|product|apex|vendor_dlkm|system_dlkm|odm_dlkm)/[A-Za-z0-9_./@+-]+$")
LOG = re.compile(r"_tv_adec|LAPAF|MatMlpOnly|MAT_TRUEHD|isAtmos|DigitalAudio_GetFormatConvert|DigitalTx|AudioTrack|AudioFlinger|AudioPolicy|earc|truehd|IEC61937|eac3|dolby|MS12|hdmi.*audio", re.I)

def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024*1024), b""):
            h.update(block)
    return h.hexdigest()

def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True)+"\n", encoding="utf-8")

def safe_remote(p):
    return SAFE_PATH.fullmatch(p) is not None and all(x not in (".", "..") for x in p.split("/")[1:])

def candidate(path):
    p = PurePosixPath(path)
    n = p.name.lower()
    if n.endswith(".so"):
        return bool(LIB.search(n))
    if n.endswith(".ko") or ".ko." in n:
        return bool(MOD.search(n))
    if n in ("modules.load", "modules.dep", "modules.softdep", "modules.alias") or n.startswith("modules.load."):
        return True
    if p.suffix.lower() in (".xml", ".conf", ".ini", ".json", ".rc", ".cfg", ".txt"):
        return bool(CONF.search(n)) or ("linkerconfig" in path and n.endswith(".txt"))
    if ("firmware" in p.parts or "dsp" in p.parts or "audio" in p.parts):
        return bool(CONF.search(n) or LIB.search(n)) and p.suffix.lower() in (".bin", ".dat", ".fw", ".tbl", ".blob")
    return False

def invoke(argv, timeout=60):
    try:
        p = subprocess.run([str(x) for x in argv], capture_output=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        return 124, e.stdout or b"", (e.stderr or b"")+b"\nTIMEOUT\n"
    except OSError as e:
        return 127, b"", str(e).encode()

class Collector:
    def __init__(self, out):
        self.out = out
        self.status = []
        self.files = []
        self.total = 0
        self.prefix = []
    def save(self, name, args, timeout=60, filter_log=False):
        args = self.prefix + args
        print(name, flush=True)
        rc, data, err = invoke(args, timeout)
        if filter_log:
            data = b"\n".join(line for line in data.splitlines()
                              if LOG.search(line.decode("utf-8", "replace")))+b"\n"
        p = self.out / "reports" / (name+".txt")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
        if err:
            p.with_suffix(".stderr.txt").write_bytes(err)
        self.status.append({"name":name,"exit":rc,"stdout_bytes":len(data),"stderr_bytes":len(err),
                            "note":"Exit 0 does not guarantee every nested shell query worked."})
        return rc, data.decode("utf-8","replace")
    def shell(self,name,cmd,timeout=60):
        return self.save(name, ["shell", cmd], timeout)
    def copy_file(self, src, dest, origin):
        size = src.stat().st_size
        if size > MAX_FILE or self.total+size > MAX_TOTAL:
            self.files.append({"origin":origin,"status":"size/budget limit","bytes":size})
            return
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            raise ValueError("Duplicate destination: "+str(dest))
        shutil.copyfile(src,dest)
        self.total += size
        self.files.append({"origin":origin,"status":"copied","bytes":size,"sha256":digest(dest),
                           "archive_path":dest.relative_to(self.out).as_posix()})
    def pull(self, remote):
        rc, data, err = invoke(self.prefix+["shell", "stat -c %s "+remote])
        try:
            size = int(data.strip())
            if rc or size < 0:
                raise ValueError()
        except ValueError:
            self.files.append({"origin":remote,"status":"stat unavailable","exit":rc,
                               "error":err.decode("utf-8","replace")[:1000]})
            return
        if size > MAX_FILE or self.total+size > MAX_TOTAL:
            self.files.append({"origin":remote,"status":"size/budget limit","bytes":size})
            return
        dest = self.out/"live_files"/remote.lstrip("/")
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            self.files.append({"origin":remote,"status":"duplicate local path skipped"})
            return
        rc, data, err = invoke(self.prefix+["pull", remote, str(dest)],120)
        if rc or not dest.is_file() or dest.stat().st_size != size:
            if dest.is_file():
                dest.unlink()
            self.files.append({"origin":remote,"status":"pull failed or size changed","exit":rc,
                               "error":(data+err).decode("utf-8","replace")[:1000]})
            return
        h = digest(dest)
        rc2, hdata, _ = invoke(self.prefix+["shell","sha256sum "+remote])
        token = hdata.decode("ascii","replace").split()
        remote_hash = token[0].lower() if rc2 == 0 and token and re.fullmatch("[0-9a-fA-F]{64}",token[0]) else None
        self.total += size
        self.files.append({"origin":remote,"status":"copied","bytes":size,"sha256":h,
                           "remote_sha256":remote_hash,"matches_remote":None if remote_hash is None else h==remote_hash,
                           "archive_path":dest.relative_to(self.out).as_posix()})
    def live(self,a):
        rc,data,err = invoke([a.adb,"devices"])
        if rc:
            raise RuntimeError("adb devices failed: "+err.decode("utf-8","replace"))
        devices = [line.split()[0] for line in data.decode("utf-8","replace").splitlines()
                   if len(line.split())>=2 and line.split()[1]=="device"]
        serial = a.serial or os.environ.get("ANDROID_SERIAL")
        if not serial:
            if len(devices)!=1:
                raise RuntimeError("Select exactly one online TV using --serial (adb devices).")
            serial = devices[0]
        if serial not in devices:
            raise RuntimeError("Selected ADB device is not online/authorized.")
        self.prefix=[a.adb,"-s",serial]
        rc,props=self.shell("identity", "for k in "+" ".join(PROPS)+'; do printf "%s=" "$k"; getprop "$k"; done')
        if rc:
            raise RuntimeError("Device identity unavailable.")
        properties=dict(line.split("=",1) for line in props.splitlines() if "=" in line)
        write_json(self.out/"identity.json",properties)
        print("Device:",properties.get("ro.product.model"),properties.get("ro.build.fingerprint"),flush=True)
        queries={
            "kernel":"uname -a",
            "selinux":"getenforce",
            "partitions":"ls -l /dev/block/by-name /dev/block/bootdevice/by-name /dev/block/mapper",
            "mounts":"cat /proc/mounts",
            "bootctl_current":"bootctl get-current-slot",
            "bootctl_slots":"bootctl get-number-slots",
            "bootctl_merge":"bootctl get-snapshot-merge-status",
            "update_status":"update_engine_client --status",
            "snapshot_status":"snapshotctl dump",
            "modules":"cat /proc/modules",
            "alsa_cards":"cat /proc/asound/cards",
            "alsa_pcm":"cat /proc/asound/pcm",
            "alsa_devices":"cat /proc/asound/devices",
            "sound_nodes":"ls -l /dev/snd /sys/class/sound",
            "sound_metadata":'for p in /sys/class/sound/card* /sys/class/sound/pcmC*D*; do [ -e "$p" ] || continue; echo "=== $p ==="; readlink -f "$p/device"; for f in id pcm_class uevent dev; do [ ! -e "$p/$f" ] || cat "$p/$f"; done; done',
            "audio_platform_bindings":'for p in /sys/bus/platform/devices/*; do case "$p" in *audio*|*sound*|*tvsnd*|*earc*|*spdif*|*offload*|*aloop*|*dsp*) echo "=== $p ==="; ls -ld "$p" "$p/driver"; readlink "$p/driver"; ;; esac; done',
            "apex":"ls -ld /apex/*",
            "services":"dumpsys -l",
            "binder_services":"service list",
            "hal_services":"lshal",
            "audio":"dumpsys audio",
            "audio_policy":"dumpsys media.audio_policy",
            "audio_flinger":"dumpsys media.audio_flinger",
            "media_codec":"dumpsys media.codec",
            "media_extractor":"dumpsys media.extractor",
            "hdmi_control":"dumpsys hdmi_control",
            "audio_vendor_services":'for s in vendor.dolby.audio.ms12.IMs12/default vendor.mediatek.hardware.audioext.IAudioExt/audioext vendor.tcl.audio.ITclAudio/default vendor.tcl.audio.ITclAudio/taudio; do echo "=== $s ==="; dumpsys -t 8 "$s"; done',
            "audio_properties":'getprop | grep -Ei "audio|dolby|truehd|earc|spdif|ms12|\\.mkv\\.|tcl.*version|firmware"',
            "module_paths":"ls -ld /vendor/lib/modules /vendor_dlkm/lib/modules /system_dlkm/lib/modules /odm/lib/modules /odm_dlkm/lib/modules",
        }
        if a.privacy_inventory:
            queries["installed_packages"]="pm list packages -f"
        for name,cmd in queries.items():
            self.shell(name,cmd,90)
        rc,data,err=invoke(self.prefix+["exec-out","cat","/proc/config.gz"])
        valid=False
        if rc==0 and data.startswith(b"\x1f\x8b") and len(data)<4*1024**2:
            try:
                with gzip.GzipFile(fileobj=__import__("io").BytesIO(data)) as f:
                    cfg=f.read(4*1024**2+1)
                valid=len(cfg)<=4*1024**2 and b"CONFIG_" in cfg
                if valid:
                    (self.out/"reports"/"kernel_config.gz").write_bytes(data)
                    (self.out/"reports"/"kernel_config.txt").write_bytes(cfg)
            except (OSError,EOFError):
                pass
        self.status.append({"name":"kernel_config","exit":rc,"valid_gzip_config":valid,
                            "error":err.decode("utf-8","replace")[:1000]})
        # Read existing log buffers only. No clearing, playback or app launch.
        if a.logs:
            self.save("audio_logcat",["logcat","-d","-t","12000","-v","threadtime"],90,True)
        dirs=" ".join("/"+p for p in ("vendor/lib","vendor/lib64","vendor/etc","vendor/firmware","vendor/bin/hw",
              "odm/lib","odm/lib64","odm/etc","odm/firmware","system/lib","system/lib64","system/etc",
              "system_ext/lib","system_ext/lib64","system_ext/etc","product/etc","product/lib","product/lib64",
              "vendor_dlkm/lib/modules","system_dlkm/lib/modules","odm_dlkm/lib/modules","apex"))
        rc,listing=self.shell("firmware_paths","find "+dirs+" -type f",180)
        # Include known symlinked clients and stock-compatible config names.
        _,links=self.shell("audio_symlink_paths","find /system/lib /system/lib64 /vendor/lib /vendor/lib64 /apex -name libaudioclient.so",120)
        _,alias=self.shell("module_alias_paths","ls -1 /vendor/lib/modules/ /odm/lib/modules/",60)
        paths=sorted({p.strip() for p in (listing+"\n"+links).splitlines()
                      if safe_remote(p.strip()) and candidate(p.strip())})
        # Direct module paths from mounted vendor_dlkm are normally in the find result.
        write_json(self.out/"live_candidates.json",paths)
        if not a.reports_only:
            for p in paths:
                self.pull(p)
        _,end=self.shell("identity_end","getprop ro.build.fingerprint")
        if end.strip()!=properties.get("ro.build.fingerprint","").strip():
            self.status.append({"name":"identity_consistency","warning":"Fingerprint changed or final query failed."})
    def offline(self,a):
        from inspect_stock_ota import inspect
        import inspect_stock_audio_dt as dtree
        from audit_stock_boot import gunzip
        inventory=None
        expected={}
        if a.ota:
            if zipfile.is_zipfile(a.ota):
                inventory=inspect(a.ota)
            else:
                inventory={"filename":a.ota.name,"bytes":a.ota.stat().st_size,
                           "sha256":digest(a.ota),"payload_status":"Non-ZIP/PKG: inventory only"}
            write_json(self.out/"ota_inventory.json",inventory)
            parts=inventory.get("payload",{}).get("partitions",[])
            for p in parts:
                if p["name"] in expected:
                    raise ValueError("Duplicate OTA partition")
                expected[p["name"]]=p
        image_dir=a.images
        if a.payload_dumper:
            payload=(inventory or {}).get("payload",{})
            if not a.ota or payload.get("minor_version")!=0 or payload.get("old_partition_info_present")!=False:
                raise ValueError("Extraction needs a full CrAU v2 OTA, not incremental/PKG.")
            if image_dir:
                raise ValueError("Use --images OR --payload-dumper, not both.")
            selected=[n for n in PARTS if n in expected]
            if not selected:
                raise ValueError("No supported partitions")
            image_dir=self.out.parent/(self.out.name+"_images")
            image_dir.mkdir(exist_ok=False)
            exe=a.payload_dumper.resolve(strict=True)
            write_json(self.out/"payload_tool.json",{"name":exe.name,"sha256":digest(exe)})
            # Explicit user-supplied local executable only; no auto-download.
            p=subprocess.run([str(exe),"-p",",".join(selected),"-o",str(image_dir),str(a.ota.resolve())])
            if p.returncode:
                raise RuntimeError("Payload extraction failed; do not use partial images.")
        images={}
        records=[]
        if image_dir:
            for n in PARTS:
                src=image_dir/(n+".img")
                if not src.is_file() or src.is_symlink():
                    continue
                h=digest(src); size=src.stat().st_size
                e=expected.get(n,{})
                ok=bool(e and e.get("new_size")==size and e.get("new_sha256","").lower()==h)
                records.append({"name":n,"bytes":size,"sha256":h,"matches_ota_manifest":ok if e else None})
                if ok:
                    images[n]=src
            write_json(self.out/"stock_image_inventory.json",records)
        # Never parse/extract an image with unknown or mismatched manifest provenance.
        result={"live_tree_verified":False,"overlays_applied":False,"warnings":[]}
        for n in ("boot","vendor_boot","dtbo"):
            if n not in images:
                continue
            try:
                src=images[n]
                if src.stat().st_size>MAX_IMAGE:
                    raise ValueError("Image exceeds audit limit")
                data=src.read_bytes()
                if n=="vendor_boot":
                    region,meta=dtree.vendor_region(data)
                    result[n]=dict(meta,**dtree.vendor_candidates(region))
                elif n=="dtbo":
                    result[n]=dtree.dtbo(data)
                else:
                    if len(data)<44 or data[:8]!=b"ANDROID!":
                        raise ValueError("Unsupported boot header")
                    size=struct.unpack_from("<I",data,8)[0]
                    ver=struct.unpack_from("<I",data,40)[0]
                    page=4096 if ver>=3 else struct.unpack_from("<I",data,36)[0]
                    if ver not in range(5) or not 512<=page<=65536 or page&(page-1) or not size or page+size>len(data):
                        raise ValueError("Unsupported boot layout")
                    k=data[page:page+size]
                    b={"header_version":ver,"kernel_bytes":size,"kernel_payload_sha256":hashlib.sha256(k).hexdigest(),
                       "config_status":"not found"}
                    if k.startswith(b"\x1f\x8b"):
                        k=gunzip(k)
                    start=k.find(b"IKCFG_ST")
                    if start>=0:
                        stop=k.find(b"IKCFG_ED",start+8)
                        if stop<0:
                            raise ValueError("Truncated config")
                        cfg=gunzip(k[start+8:stop])
                        (self.out/"stock_kernel_config.txt").write_bytes(cfg)
                        b["config_status"]="extracted"
                    write_json(self.out/"stock_boot_audit.json",b)
            except (ValueError, OSError, struct.error) as e:
                result["warnings"].append(n+": "+str(e))
        write_json(self.out/"stock_audio_dt_topology.json",result)
        if a.extract_filesystems:
            if os.name!="posix" or not hasattr(os,"geteuid") or os.geteuid()==0:
                raise ValueError("Run --extract-filesystems as normal Linux/WSL user.")
            from compare_stock_audio import extract,walk,unzip_member
            for tool in ("fsck.erofs","debugfs"):
                if not shutil.which(tool):
                    raise ValueError("Missing "+tool)
            with tempfile.TemporaryDirectory(prefix="tcl_audit_",dir=Path.home()) as work:
                work=Path(work)
                for n in PARTS:
                    if n not in images or n in ("boot","init_boot","vendor_boot","dtbo","mboot","vbmeta"):
                        continue
                    try:
                        base=work/n
                        extract(images[n],base)
                        for src in walk(base):
                            rel=src.relative_to(base).as_posix()
                            if candidate("/"+n+"/"+rel):
                                self.copy_file(src,self.out/"stock_files"/n/rel,n+":/"+rel)
                            if src.suffix.lower() in (".apex",".capex") and CONF.search(src.name):
                                try:
                                    nested=Path(tempfile.mkdtemp(prefix="apex_",dir=work))
                                    ap=src
                                    if src.suffix.lower()==".capex":
                                        ap=nested/"original.apex"
                                        unzip_member(src,"original_apex",ap)
                                    pi=nested/"apex_payload.img"
                                    unzip_member(ap,"apex_payload.img",pi)
                                    extract(pi,nested/"payload")
                                    for child in walk(nested/"payload"):
                                        r=child.relative_to(nested/"payload").as_posix()
                                        if candidate("/apex/"+r):
                                            self.copy_file(child,self.out/"stock_apex"/n/rel/r,n+":/"+rel+"!/"+r)
                                except (ValueError,OSError,zipfile.BadZipFile,subprocess.CalledProcessError) as e:
                                    self.status.append({"name":"apex:"+rel,"error":str(e)})
                    except (ValueError,OSError,subprocess.CalledProcessError) as e:
                        self.status.append({"name":"filesystem:"+n,"error":str(e)})
        # Compare by path suffix only when unambiguous; preserve unknowns separately.
        live=[x for x in self.files if x.get("archive_path","").startswith("live_files/")]
        stock=[x for x in self.files if x.get("archive_path","").startswith("stock_files/")]
        comparisons=[]
        for l in live:
            loc=l["origin"].lstrip("/")
            matches=[s for s in stock if s["archive_path"].removeprefix("stock_files/") in (loc,loc.split("/",1)[0]+"/"+loc)]
            for s in matches:
                comparisons.append({"live":l["origin"],"stock":s["origin"],"matches":l["sha256"]==s["sha256"]})
        write_json(self.out/"stock_live_comparison.json",comparisons)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--label",required=True,help="e.g. C655-stock or C655-truehd")
    p.add_argument("--serial",help="ADB serial; required if multiple connected")
    p.add_argument("--adb",default="adb")
    p.add_argument("--reports-only",action="store_true",help="skip ADB binary pulls")
    p.add_argument("--logs",action="store_true",help="save filtered existing audio logcat")
    p.add_argument("--privacy-inventory",action="store_true",help="include installed package paths")
    p.add_argument("--offline-only",action="store_true")
    p.add_argument("--ota",type=Path)
    p.add_argument("--images",type=Path,help="already extracted raw partition images")
    p.add_argument("--payload-dumper",type=Path,help="explicit local trusted payload-dumper-go executable")
    p.add_argument("--extract-filesystems",action="store_true",help="Linux/WSL only: extract verified image files offline")
    a=p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}",a.label):
        p.error("Label must be simple ASCII letters/digits/_/-")
    if a.offline_only and not a.ota:
        p.error("--offline-only requires --ota")
    if (a.images or a.payload_dumper or a.extract_filesystems) and not a.ota:
        p.error("Offline image verification requires --ota")
    if a.extract_filesystems and not (a.images or a.payload_dumper):
        p.error("--extract-filesystems needs --images or --payload-dumper")
    stamp=dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out=ROOT/"private"/"device_audits"/(a.label+"_"+stamp)
    out.mkdir(parents=True,exist_ok=False)
    c=Collector(out)
    failed=False
    try:
        if not a.offline_only:
            c.live(a)
        if a.ota:
            c.offline(a)
    except (ValueError,RuntimeError,OSError,zipfile.BadZipFile,subprocess.CalledProcessError) as e:
        c.status.append({"fatal":str(e)})
        failed=True
    finally:
        write_json(out/"collection_status.json",{"partial":failed,"device_writes":False,
                   "ota_signature_verified":False,"installed_image_identity_verified":False,
                   "commands":c.status,"files":c.files})
        archive=out.with_suffix(".zip")
        with zipfile.ZipFile(archive,"x",zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for path in sorted(out.rglob("*")):
                if path.is_file() and not path.is_symlink():
                    z.write(path,path.relative_to(out).as_posix())
        print("Saved private evidence:",archive)
        print("Review before sharing. No root/unlock/flash/reboot or playback performed.")
    return 1 if failed else 0

if __name__=="__main__":
    raise SystemExit(main())
