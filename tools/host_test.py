#!/usr/bin/env python3
"""Create synthetic test media, run parser and chunk identity tests (requires FFmpeg with TrueHD encoder and JDK)."""
import pathlib, subprocess, tempfile, sys
root=pathlib.Path(__file__).resolve().parents[1]
def run(*args):subprocess.run(list(map(str,args)),check=True)
with tempfile.TemporaryDirectory() as d:
    d=pathlib.Path(d);source=d/'src.thd';eac=d/'src.eac3';out=d/'samples';classes=d/'classes';classes.mkdir()
    run('ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t','6','-c:a','truehd','-strict','-2','-f','truehd',source)
    run('ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','anullsrc=r=48000:cl=5.1','-t','6','-c:a','eac3','-f','eac3',eac)
    run(sys.executable,root/'tools/prepare_samples.py',source,'--eac3',eac,'--seconds','6','--out',out)
    java=root/'app/src/main/java/ro/saitatter/audioprobe'
    run('javac','--release','11','-d',classes,java/'FrameReader.java',java/'TrueHdChunks.java',root/'tools/FrameReaderTest.java',root/'tools/TrueHdChunksTest.java')
    run('java','-cp',classes,'ro.saitatter.audioprobe.FrameReaderTest',out)
    run('java','-cp',classes,'ro.saitatter.audioprobe.TrueHdChunksTest',out/'truehd.thd')
