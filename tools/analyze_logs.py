#!/usr/bin/env python3
"""Extract selected diagnostics from local log files or ZIPs; output remains private until reviewed."""
import argparse, pathlib, re, json, zipfile
PATTERNS={
 'probe_test':r'=== TEST |=== TUNNEL TEST ', 'observed':r'OBSERVED ', 'probe_built':r'\bBUILT session=|AUDIO_BUILT', 'probe_end':r'\bTUNNEL_END |\bEND bytes=',
 'iec_filter':r'IEC61937.*(?:support|track)|(?:support|track).*IEC61937', 'hal_route':r'DigitalOutputFormat|Input format\(|updatePipelineTopology: .*EarcOutput|SEBypassOutput',
 'atmos':r'MAT stream type|DolbyInfo\.is_atmos|STREAM_ATOMS|hasAtmos:|isAtmos\(', 'avsync':r'hwSyncId|hw_sync_id|HW_AV_SYNC|tpc is not ready', 'frame_errors':r'Too long without an audio frame|no tpc audio hal handle fail', 'dsp':r'ADEC1 ESID|ch_map\(', 'exceptions':r'FAIL:|Cannot create AudioTrack'}
REGEX={k:re.compile(v,re.I) for k,v in PATTERNS.items()}
def decode(data):return data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig',errors='replace')
def sources(p):
    for f in sorted(p.rglob('*')) if p.is_dir() else [p]:
        if not f.is_file():continue
        if f.suffix=='.zip':
            with zipfile.ZipFile(f) as z:
                for n in z.namelist():
                    if pathlib.Path(n).suffix in ('.log','.txt'):yield f.name+'!'+n,decode(z.read(n))
        elif f.suffix in ('.txt','.log'):yield f.name,decode(f.read_bytes())
def main():
    p=argparse.ArgumentParser();p.add_argument('path',type=pathlib.Path);p.add_argument('--out',type=pathlib.Path,default=pathlib.Path('private/log-analysis.json'));p.add_argument('--limit',type=int,default=20,help='First and last matching lines per category');a=p.parse_args();results=[]
    if not a.path.exists():p.error('Input missing')
    for name,txt in sources(a.path):
        lines=txt.splitlines();report={}
        for key,rx in REGEX.items():
            m=[{'line':i,'text':line} for i,line in enumerate(lines,1) if rx.search(line)]
            if m:report[key]={'count':len(m),'first':m[:a.limit],'last':m[-a.limit:] if a.limit else []}
        if report:results.append({'source':name,'lines':len(lines),'matches':report})
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(results,indent=2),encoding='utf-8');print(f'{len(results)} reports; review output before sharing: {a.out}')
if __name__=='__main__':main()
