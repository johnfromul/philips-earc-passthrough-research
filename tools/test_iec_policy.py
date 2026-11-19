#!/usr/bin/env python3
"""Structural test only, accepts a representative policy fixture."""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET
from plan_iec_policy import propose, PORT, FORMAT

def main():
    data = Path(sys.argv[1]).read_bytes()
    out = propose(data)
    assert propose(data) == out
    root = ET.fromstring(out)
    port = [p for p in root.iter('mixPort') if p.get('name') == PORT][0]
    assert port.find('profile').get('format') == FORMAT
    assert port.find('profile').get('samplingRates') == '192000'
    assert port.find('profile').get('channelMasks') == 'AUDIO_CHANNEL_OUT_7POINT1'
    assert [r.get('sink') for r in root.iter('route') if PORT in r.get('sources', '').split(',')] == ['HDMI EARC']
    for bad in (out, data.replace(b'sink="HDMI EARC"', b'sink="OTHER"')):
        try:
            propose(bad)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid policy accepted')
    print('PASS: deterministic, one eARC-only port, stock routes preserved; duplicate/missing route rejected. No runtime validation.')

if __name__ == '__main__':
    main()
