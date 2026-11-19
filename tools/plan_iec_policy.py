#!/usr/bin/env python3
"""Create an OFFLINE policy candidate; not a deployable audio configuration."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

STOCK_SHA256 = '4fbbdd581ec2415d04aab5edfc139a5585af10cac9ed85bbd4ab523dc0518884'
PORT = 'iec61937 earc sync experiment'
FORMAT = 'AUDIO_FORMAT_IEC61937'


def propose(data):
    root = ET.fromstring(data)
    if root.tag != 'module' or root.get('name') != 'primary':
        raise ValueError('Expected primary audio policy module')
    mixes, devices, routes = (root.find(tag) for tag in ('mixPorts', 'devicePorts', 'routes'))
    if any(x is None for x in (mixes, devices, routes)):
        raise ValueError('Missing required policy sections')
    if any(p.get('format') == FORMAT for p in root.iter('profile')):
        raise ValueError('Existing IEC profile; refusing to merge')
    if PORT in data.decode('utf-8'):
        raise ValueError('Experimental port already present')
    earc = [d for d in devices if d.get('tagName') == 'HDMI EARC']
    if len(earc) != 1 or earc[0].get('type') != 'AUDIO_DEVICE_OUT_HDMI_EARC' or earc[0].get('role') != 'sink':
        raise ValueError('Expected unique eARC sink')
    links = [r for r in routes if r.get('sink') == 'HDMI EARC']
    if len(links) != 1 or links[0].get('type') != 'mix':
        raise ValueError('Expected unique eARC mix route')
    # Keep original bytes, comments, includes, whitespace and all other ports intact.
    nl = b'\r\n' if b'\r\n' in data else b'\n'
    block = nl.join([b'        <!-- OFFLINE CANDIDATE: requires validated HAL eARC non-PCM transport -->',
        ('        <mixPort name="%s" role="source"' % PORT).encode(),
        b'                 flags="AUDIO_OUTPUT_FLAG_DIRECT AUDIO_OUTPUT_FLAG_HW_AV_SYNC" maxOpenCount="1" maxActiveCount="1">',
        b'            <profile name="" format="AUDIO_FORMAT_IEC61937" samplingRates="192000"',
        b'                     channelMasks="AUDIO_CHANNEL_OUT_7POINT1"/>',
        b'        </mixPort>', b''])
    close = b'    </mixPorts>'
    if data.count(close) != 1:
        raise ValueError('Unexpected mixPorts layout')
    out = data.replace(close, block + close, 1)
    route = re.compile(rb'(<route\b[^>]*\bsink="HDMI EARC"[^>]*\bsources=")([^"]+)("[^>]*>)')
    m = list(route.finditer(out))
    if len(m) != 1:
        raise ValueError('Unexpected eARC route layout')
    match = m[0]
    out = out[:match.end(2)] + b',' + PORT.encode() + out[match.end(2):]
    # Verify the only semantic additions are one port and its eARC route reference.
    check = ET.fromstring(out)
    new_mixes = check.find('mixPorts')
    new_port = [p for p in new_mixes if p.get('name') == PORT]
    assert len(new_port) == 1
    new_links = [r for r in check.find('routes') if PORT in r.get('sources', '').split(',')]
    assert len(new_links) == 1 and new_links[0].get('sink') == 'HDMI EARC'
    new_links[0].set('sources', links[0].get('sources'))
    new_mixes.remove(new_port[0])
    def structure(node):
        return (node.tag, sorted(node.attrib.items()), (node.text or '').strip(), tuple(structure(c) for c in node))
    assert structure(check) == structure(root)
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stock_policy', type=Path)
    p.add_argument('--out-dir', type=Path, default=Path('private/iec_policy_experiment'))
    args = p.parse_args()
    data = args.stock_policy.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != STOCK_SHA256:
        raise SystemExit('Not the exact V670 policy; refusing to generate candidate.')
    out = propose(data)
    args.out_dir.mkdir(parents=True, exist_ok=False)
    (args.out_dir / 'primary_audio_policy_configuration.experimental.xml').write_bytes(out)
    report = {'stock_sha256': sha, 'candidate_sha256': hashlib.sha256(out).hexdigest(),
              'port': PORT, 'format': FORMAT, 'rate': 192000, 'channels': 8,
              'route': 'HDMI EARC', 'flags': ['DIRECT', 'HW_AV_SYNC'],
              'device_port_unchanged': True, 'runtime_validated': False, 'flashable': False,
              'warning': 'Offline only. No validated HAL transport or runtime capability advertisement.'}
    (args.out_dir / 'policy_report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('Saved offline candidate, not for deployment:', args.out_dir)


if __name__ == '__main__':
    main()
