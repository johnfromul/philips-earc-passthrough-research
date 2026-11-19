#!/usr/bin/env python3
"""Prepare lossless TrueHD access units and IEC/MAT using a local FFmpeg installation."""
import argparse, json, pathlib, struct, subprocess, hashlib

def run(*args):
    subprocess.run(list(map(str,args)),check=True)

def probe(path):
    return json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(path)]))['streams']

def choose(path,codec,index):
    streams=probe(path)
    matches=[s for s in streams if s.get('codec_type')=='audio' and s.get('codec_name')==codec and (index is None or s['index']==index)]
    if not matches: raise ValueError(f'No {codec} audio stream (index={index}) in {path}')
    s=matches[0]
    if int(s['sample_rate'])!=48000: raise ValueError('This initial diagnostic targets 48 kHz source tracks only')
    print(f"Selected {codec} stream {s['index']}, {s.get('channels')} channels, profile {s.get('profile')}. Verify Atmos independently.")
    return s

def split_mat(iec,be,le):
    count=0
    with iec.open('rb') as inp, be.open('wb') as big, le.open('wb') as little:
        while True:
            burst=inp.read(61440)
            if not burst: break
            if len(burst)!=61440: raise ValueError('Truncated IEC burst')
            pa,pb,pc,pd=struct.unpack_from('<4H',burst)
            if (pa,pb,pc&31,pd)!=(0xf872,0x4e1f,0x16,61424): raise ValueError(f'Unexpected TrueHD IEC header {(pa,pb,pc,pd)}')
            payload=burst[8:8+pd]
            if payload[:4]!=bytes.fromhex('9e070300'): raise ValueError('Unexpected MAT sync')
            swapped=bytearray(payload)
            swapped[::2]=payload[1::2]; swapped[1::2]=payload[::2]
            little.write(payload); big.write(swapped); count+=1
    if not count: raise ValueError('No complete MAT bursts')
    print(f'Validated {count} IEC bursts / MAT payloads, nominal {count*0.02:.2f} seconds')

def main():
    p=argparse.ArgumentParser(); p.add_argument('truehd',type=pathlib.Path); p.add_argument('--eac3',type=pathlib.Path); p.add_argument('--truehd-index',type=int); p.add_argument('--eac3-index',type=int); p.add_argument('--seconds',type=int,default=30); p.add_argument('--out',type=pathlib.Path,default=pathlib.Path('samples')); a=p.parse_args()
    if not 5<=a.seconds<=120: p.error('--seconds must be 5..120')
    out=a.out; out.mkdir(parents=True,exist_ok=True)
    s=choose(a.truehd,'truehd',a.truehd_index)
    common=['ffmpeg','-hide_banner','-loglevel','warning','-y','-i',str(a.truehd),'-map',f"0:{s['index']}",'-t',str(a.seconds)]
    run(*common,'-c:a','copy','-f','truehd',out/'truehd.thd')
    run(*common,'-c:a','copy','-f','spdif',out/'truehd.iec')
    split_mat(out/'truehd.iec',out/'truehd.matbe',out/'truehd.matle')
    run(*common,'-c:a','pcm_s16le','-ac','2','-ar','48000','-f','s16le',out/'pcm.s16le')
    files=['truehd.thd','truehd.iec','truehd.matbe','truehd.matle','pcm.s16le']
    if a.eac3:
        e=choose(a.eac3,'eac3',a.eac3_index)
        run('ffmpeg','-hide_banner','-loglevel','warning','-y','-i',a.eac3,'-map',f"0:{e['index']}",'-t',a.seconds,'-c:a','copy','-f','eac3',out/'control.eac3'); files.append('control.eac3')
    manifest={'truehd_source':a.truehd.name,'truehd_stream':s,'seconds':a.seconds,'files':{name:{'bytes':(out/name).stat().st_size,'sha256':hashlib.sha256((out/name).read_bytes()).hexdigest()} for name in files}}
    if a.eac3: manifest['eac3_source']=a.eac3.name; manifest['eac3_stream']=e
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print('Samples ready in',out.resolve())
if __name__=='__main__':main()
