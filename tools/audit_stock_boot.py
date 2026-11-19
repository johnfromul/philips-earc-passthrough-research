#!/usr/bin/env python3
"""Read-only Android boot-header and embedded kernel config audit. No TV access."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAX = 256 * 1024 * 1024
STOCK = '3d2fdf5363d86061f7b30855458e673a39cada336ec0359a12a11533362793e9'
KEYS = ('MODULES', 'MODULE_UNLOAD', 'MODVERSIONS', 'MODULE_SIG', 'MODULE_SIG_FORCE',
        'MODULE_SIG_ALL', 'MODULE_COMPRESS', 'CFI_CLANG', 'CFI_PERMISSIVE',
        'SHADOW_CALL_STACK', 'DEBUG_INFO_BTF', 'DEBUG_INFO_BTF_MODULES',
        'IKCONFIG', 'IKCONFIG_PROC', 'SND', 'SND_SOC', 'SND_COMPRESS_OFFLOAD')


def gunzip(data):
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    out = d.decompress(data, MAX + 1)
    if len(out) > MAX or not d.eof:
        raise ValueError('truncated or oversized gzip stream')
    return out


def audit(image, manifest):
    report = json.loads(manifest.read_text(encoding='utf-8-sig'))
    if report.get('stock_sha256') != STOCK:
        raise ValueError('not the expected stock OTA extraction report')
    entries = [p for p in report['partitions'] if p['name'] == 'boot']
    if len(entries) != 1:
        raise ValueError('expected exactly one boot partition')
    p = entries[0]
    if image.stat().st_size > MAX:
        raise ValueError('image exceeds audit size limit')
    data = image.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if not p.get('matches') or len(data) != p['expected_bytes'] or digest != p['expected_sha256'].lower():
        raise ValueError('boot image does not match the extraction manifest')
    if len(data) < 44 or data[:8] != b'ANDROID!':
        raise ValueError('unsupported Android boot magic/header')
    size = struct.unpack_from('<I', data, 8)[0]
    version = struct.unpack_from('<I', data, 40)[0]
    if version not in range(5):
        raise ValueError('unsupported boot header version')
    page = 4096 if version >= 3 else struct.unpack_from('<I', data, 36)[0]
    if page < 512 or page > 65536 or page & (page-1) or size == 0 or page + size > len(data):
        raise ValueError('invalid kernel region')
    kernel = data[page:page+size]
    result = dict(boot_sha256=digest, boot_bytes=len(data), header_version=version,
                  page_size=page, kernel_bytes=size, kernel_payload_sha256=hashlib.sha256(kernel).hexdigest(),
                  compression='unknown/raw', config_status='not found', config={})
    if kernel.startswith(b'\x1f\x8b'):
        result['compression'] = 'gzip'
        kernel = gunzip(kernel)
    elif kernel.startswith((b'\x04\x22\x4d\x18', b'\x02\x21\x4c\x18')):
        result['compression'] = 'lz4 (not decoded by this tool)'
    elif kernel.startswith(b'\xfd7zXZ\x00'):
        result['compression'] = 'xz (not decoded by this tool)'
    elif kernel[56:60] == b'ARM\x64':
        result['compression'] = 'raw arm64 Image'
    start = kernel.find(b'IKCFG_ST')
    if start >= 0:
        stop = kernel.find(b'IKCFG_ED', start+8)
        if stop < 0:
            raise ValueError('incomplete embedded config marker')
        cfg = gunzip(kernel[start+8:stop]).decode('ascii')
        for line in cfg.splitlines():
            for key in KEYS:
                k = 'CONFIG_' + key
                if line.startswith(k + '='):
                    result['config'][k] = line.split('=', 1)[1]
                elif line == '# ' + k + ' is not set':
                    result['config'][k] = 'n'
        result['config_status'] = 'embedded config extracted (selected keys only)'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, default=ROOT/'private/stock_v670_us_au02/boot_audit_images/boot.img')
    parser.add_argument('--manifest', type=Path, default=ROOT/'private/stock_boot_audit_verified.json')
    parser.add_argument('--out', type=Path, default=ROOT/'private/stock_kernel_config_audit.json')
    args = parser.parse_args()
    result = audit(args.boot, args.manifest)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('Saved:', args.out, '—', result['config_status'])


if __name__ == '__main__':
    main()
