#!/usr/bin/env python3
"""Inspect elementary TrueHD AU boundaries; no decoding, rewriting or device access."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def inspect(path):
    digest = hashlib.sha256()
    counts, gaps, phases, rates = Counter(), Counter(), Counter(), Counter()
    starts, sync_starts, runs = {}, Counter(), []
    n = offset = 0
    previous_sync = previous_timing = None
    first_sync = None
    with path.open('rb') as f:
        while True:
            h = f.read(4)
            if not h:
                break
            if len(h) != 4:
                raise ValueError(f'Truncated header at {offset}')
            size = ((h[0] & 15) * 256 + h[1]) * 2
            if not 8 <= size <= 8190:
                raise ValueError(f'Invalid AU length {size} at {offset}')
            body = f.read(size - 4)
            if len(body) != size - 4:
                raise ValueError(f'Truncated AU at {offset}')
            digest.update(h + body)
            t = int.from_bytes(h[2:4], 'big')
            if previous_timing is not None:
                counts[(t - previous_timing) & 65535] += 1
            previous_timing = t
            if len(body) >= 10 and body[:4] == bytes.fromhex('f8726fba') and body[8:10] == bytes.fromhex('b752'):
                if first_sync is None:
                    first_sync = n
                if previous_sync is not None:
                    gaps[n - previous_sync] += 1
                previous_sync = n
                phases[n % 24] += 1
                for step in (16, 24):
                    if n % step == 0:
                        sync_starts[step] += 1
                rates[body[4] >> 4] += 1
                if len(runs) < 12:
                    runs.append({'au': n, 'offset': offset, 'timing': t})
            for step in (16, 24):
                if n % step == 0:
                    starts[step] = starts.get(step, 0) + 1
            n += 1
            offset += size
    if not n or first_sync is None:
        raise ValueError('No verified TrueHD major-sync header candidate found')
    return {'bytes': offset, 'sha256': digest.hexdigest(), 'access_units': n,
            'first_major_sync_au': first_sync, 'major_sync_count': sum(phases.values()),
            'major_sync_gaps_au': dict(sorted(gaps.items())),
            'major_sync_phase_mod24': dict(sorted(phases.items())),
            'major_sync_rate_codes': dict(sorted(rates.items())),
            'timing_delta_samples_histogram': dict(sorted(counts.items())),
            'frame_reader_fallback_deltas': sum(v for k, v in counts.items() if not 0 < k <= 1920),
            'fixed_group_counts_including_tail': starts,
            'fixed_group_starts_with_major_sync': {step: sync_starts[step] for step in (16, 24)},
            'last_group24_au_count': n % 24 or 24,
            'first_major_sync_examples': runs,
            'scope': 'Header candidates and AU sizes only; not CRC, Atmos, valid payload or DSP verification'}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', type=Path)
    ap.add_argument('--out', type=Path, default=Path('private/truehd_alignment.json'))
    args = ap.parse_args()
    try:
        report = inspect(args.input)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open('x', encoding='utf-8') as f:
            json.dump(report, f, indent=2)
            f.write('\n')
    except (OSError, ValueError) as exc:
        ap.exit(1, str(exc) + '\n')
    print('Saved:', args.out)

if __name__ == '__main__':
    main()
