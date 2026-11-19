#!/usr/bin/env python3
"""Read-only ELF inventory; local output may contain copyrighted strings. Never publish raw output."""
import argparse, hashlib, json, pathlib, re, shutil, subprocess
KEYS = [b'remove support direct IEC61937', b'not support direct IEC61937', b'not support offload IEC61937', b'can not create track', b'can create track', b'/proc/%d/cmdline', b'MAT_TRUEHD', b'MAT_ATMOS', b'out_pts_detection', b'DigitalAudio_GetFormatConvert', b'DolbyInfo.is_atmos']
SYMBOLS = re.compile(r'getDirectProfilesForAttributes|getDirectPlaybackSupport|isOffloadSupported|AudioTrack.*set|openDirectOutput|getProfileForOutput|searchCompatibleProfileHwModules|SPDIFEncoder.*isFormatSupported|_dspCommFunc_LAPAF_IsAtmosStream|_dspCommFunc_LAPAF_GetMatType|_decCommFunc_Lapaf_FilterParamEvent_Update|_MI_LAPAF_UTILS_GetAudioInfo2|MI_APAF_ALSA_UTILS_LAPAF_GetKcontrolParams|DigitalAudio_GetFormatConvert|utils_ApplyOutputPath|__utils_judge_Metadata_source|mat_enc_dthd_scan_for_au|mat_enc_process_chunk|mat_enc_pack_emdf_payloads|mat_dec_evo_open|mat_enc_pcm_add_bytes|DecoderPassThrough|MediaPlayerService.*AudioOutput.*open')
def call(args):
    return subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',check=False).stdout
def main():
    p=argparse.ArgumentParser();p.add_argument('root',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path,default=pathlib.Path('private/native-analysis'));p.add_argument('--disassemble',action='store_true');a=p.parse_args()
    if not a.root.is_dir():p.error('Input directory missing')
    a.out.mkdir(parents=True,exist_ok=True);records=[];readelf=shutil.which('readelf') or shutil.which('llvm-readelf');objdump=shutil.which('llvm-objdump')
    for f in sorted(a.root.rglob('*.so')):
        b=f.read_bytes()
        if b[:4]!=b'\x7fELF':continue
        name=f.relative_to(a.root).as_posix();found={k.decode():[m.start() for m in re.finditer(re.escape(k),b)] for k in KEYS if k in b}
        r={'path':name,'size':len(b),'sha256':hashlib.sha256(b).hexdigest(),'string_file_offsets':found}
        if readelf:
            syms=call([readelf,'-Ws',str(f)]);r['symbols']=[line.strip() for line in syms.splitlines() if SYMBOLS.search(line)];r['needed']=[line.strip() for line in call([readelf,'-d',str(f)]).splitlines() if '(NEEDED)' in line]
            if a.disassemble and objdump and r['symbols']:
                # Disassemble only matching symbols for local diagnosis; ARM mapping symbols guide Thumb decoding.
                names=sorted({line.split()[-1] for line in r['symbols'] if len(line.split())>=8 and line.split()[3]=='FUNC' and line.split()[6]!='UND'})
                if names:
                    path=a.out/(name.replace('/','__')+'.asm.txt');path.write_text(call([objdump,'-d','--disassemble-symbols='+','.join(names),str(f)]),encoding='utf-8')
        records.append(r)
    (a.out/'inventory.json').write_text(json.dumps({'readelf_available':bool(readelf),'objdump_available':bool(objdump),'libraries':records},indent=2),encoding='utf-8');print(f'{len(records)} ELF files; saved {a.out}/inventory.json (private)')
if __name__=='__main__':main()
