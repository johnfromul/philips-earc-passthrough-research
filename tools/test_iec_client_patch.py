#!/usr/bin/env python3
"""Host-side structural checks against an owner's exact stock ELF, never executes ARM code."""
import argparse
from pathlib import Path
from patch_iec_client import patch, digest, STOCK_SHA256

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('stock_client',type=Path)
    args=p.parse_args()
    b=args.stock_client.read_bytes()
    a,r=patch(b)
    assert patch(b)[0]==a
    assert len(a)==len(b)==702872
    assert digest(b)==STOCK_SHA256
    assert digest(a)=='4f3aabd50c78faa3f19acc0a6078691161dea3c14fda92772247b54875a78d25'
    assert r['changed_bytes']==11
    for bad in (b[:-1],b[:1]+bytes([b[1]^1])+b[2:],a):
        try:
            patch(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted invalid input')
    for c in r['changes']:
        va=int(c['va'],16)
        imm=int.from_bytes(bytes.fromhex(c['after'])[:2],'little')&0x7ff
        imm=imm-2048 if imm&1024 else imm
        assert va+4+imm*2==int(c['target'],16)
    print('PASS: deterministic, size preserved, stock unchanged, invalid inputs rejected, branch targets verified. No runtime validation.')

if __name__=='__main__':
    main()
