#!/usr/bin/env python3
"""Generate original synthetic controls, never encode or embed Atmos."""
import argparse, pathlib, subprocess, math, struct
p=argparse.ArgumentParser();p.add_argument('--out',type=pathlib.Path,default=pathlib.Path('tunnel_samples'));p.add_argument('--force',action='store_true');a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
v=a.out/'tunnel_video.mp4';pcm=a.out/'tunnel_pcm51.s16le'
if not a.force and (v.exists() or pcm.exists()):p.error('Control files exist; use --force to replace them')
subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-y','-f','lavfi','-i','testsrc2=size=640x360:rate=30','-t','30','-c:v','libx264','-profile:v','baseline','-pix_fmt','yuv420p','-bf','0','-g','30',str(v)],check=True)
# 5.1 layout: FL FR FC LFE BL BR; two quiet clean front-channel tones.
with pcm.open('wb') as f:
    for second in range(30):
        data=bytearray()
        for n in range(48000):
            data.extend(struct.pack('<6h',round(1600*math.sin(2*math.pi*440*n/48000)),round(1600*math.sin(2*math.pi*550*n/48000)),0,0,0,0))
        f.write(data)
print('Generated',v,pcm)
