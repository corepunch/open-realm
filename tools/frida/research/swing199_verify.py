#!/usr/bin/env python3
"""Verify repeated explicit weapon windups and an observer-free public control."""
import argparse
import hashlib
import json
import re
from pathlib import Path

EVENTS = ('marker', 'cooldown-begin', 'cooldown-end', 'cap-begin', 'cap-end', 'cap-arm', 'cap-bit')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def stages(rows):
    result = [r for r in rows if r['event'] in EVENTS]
    swings = [r for r in result if r['event'] == 'cooldown-begin' and r['swing']]
    begins = [r for r in result if r['event'] == 'cap-begin']
    arms = [r for r in result if r['event'] == 'cap-arm']
    if len(swings) != 4 or len(begins) != 4 or len(arms) != 4:
        raise ValueError('missing explicit weapon producer')
    if [r['caller'] for r in swings].count('49553d') != 3 or [r['caller'] for r in swings].count('49a58b') != 1:
        raise ValueError('missing targeted or ground swing')
    for swing, begin, arm in zip(swings, begins, arms):
        if begin['caller'] != '49d145' or arm['duration'] != 0x40400000 or arm['ability']['request'] is not None:
            raise ValueError('exemption producer/deadline differs')
        if any(r['ability']['unit'] != swing['ability']['unit'] or r['c'] != swing['c'] for r in (begin, arm)):
            raise ValueError('swing and exemption identity/order differ')
        i = result.index(swing)
        if result[i + 1] != begin or result[i + 2]['event'] != 'cap-bit' or result[i + 2]['value'] != 1 or result[i + 3] != arm:
            raise ValueError('exemption did not precede cooldown completion')
    if len({tuple(r['ability']['unit']) for r in swings}) != 4:
        raise ValueError('duplicate actor instead of four weapon scenes')
    if sum(r['event'] == 'cooldown-begin' and not r['swing'] for r in result) != 7:
        raise ValueError('non-swing cooldown controls differ')
    if [r['value'] for r in result if r['event'] == 'cap-bit'].count(0) != 4:
        raise ValueError('exemption expiry incomplete')
    return result


def verify(expected, archive):
    public, observed = [], []
    for name, pin in expected['captures'].items():
        raw = (archive / name).read_bytes()
        if len(raw) != pin['bytes'] or digest(raw) != pin['sha256']:
            raise ValueError('capture pin differs')
        rows = [json.loads(line) for line in raw.splitlines()]
        meta = {k: v for k, v in rows[0].items() if k != 'pid'}
        if meta != pin['metadata'] or meta['sha256'] != expected['binary_sha256'] or not meta['owned']:
            raise ValueError('capture provenance differs')
        if any(r.get('type') == 'error' or r['event'] == 'trace-failed' for r in rows):
            raise ValueError('failed capture')
        preload = (archive / (Path(name).stem + '-preload.txt')).read_bytes()
        footer = [r for r in rows if r['event'] == 'preload-file']
        if len(footer) != 1 or not footer[0]['complete'] or digest(preload) != footer[0]['sha256']:
            raise ValueError('preload pin differs')
        markers = re.findall(r'call Preload\( "(S199 [^"\r\n]+)" \)', preload.decode())
        if len(markers) != 83 or footer[0]['markers'] != 83:
            raise ValueError('incomplete public lifetime')
        public.append(markers)
        if meta['mode'] == 'observe':
            if [r['value'] for r in rows if r['event'] == 'marker'][2:] != markers:
                raise ValueError('observer differs from public preload')
            finish = [r for r in rows if r['event'] == 'trace-end']
            if len(finish) != 1 or finish[0]['installed'] is not True:
                raise ValueError('observer not completed')
            observed.append(stages(rows))
        elif meta['mode'] != 'control' or any(r['event'] not in
                ('metadata', 'loading-key', 'control-start-file', 'preload-file') for r in rows):
            raise ValueError('control contains instrumentation')
    if len(observed) != 2 or observed[0] != observed[1] or observed[0] != expected['stages']:
        raise ValueError('retail producer streams differ')
    if len(public) != 3 or any(p != public[0] for p in public) or public[0] != expected['public_markers']:
        raise ValueError('observer-free continuation differs')
    if digest((archive / 'RS-Swing199.w3m').read_bytes()) != expected['map_sha256']:
        raise ValueError('map pin differs')
    return dict(passed=True, status='live-explicit-swing-exemption', observations=2, controls=1,
                public_markers=83, producer_rows=len(observed[0]), windups=4, non_swing_calls=7,
                binary_sha256=expected['binary_sha256'])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('expected', 'archive', 'output'):
        ap.add_argument('--' + name, type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        ap.error('report must be fresh')
    report = verify(json.loads(args.expected.read_text()), args.archive)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
