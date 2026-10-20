#!/usr/bin/env python3
"""Verify public blocked Move recovery, Shift successor and stopped path cleanup."""
import argparse
import gzip
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EMPTY = [0xffffffff, 0xffffffff]


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def recovery(rows):
    blocked = [r for r in rows if r['event'] == 'blocked-completion']
    if len(blocked) != 1 or blocked[0]['c'] != 1319 or blocked[0]['partial'] != 0:
        raise ValueError('missing final blocked completion')
    # Mouse admission time and hover references are presentation observations.
    # Compare the complete synchronous recovery, including absolute references;
    # compare the successor cleanup independently, not its intervening hover state.
    chain = [r for r in rows if r.get('c') == 1319]
    begin = next(r for r in chain if r['event'] == 'recover-begin')
    end = next(r for r in chain if r['event'] == 'recover-end')
    if begin['unit']['count'] != 2 or end['unit']['count'] != 1:
        raise ValueError('wrong FIFO recovery')
    if end['unit']['orders'] != begin['unit']['tail'] or end['unit']['tasks'] == EMPTY:
        raise ValueError('pending head did not activate')
    if begin['unit']['refs'] != end['unit']['refs']:
        raise ValueError('recovery leaked Unit references')
    prepends = [(r['event'], r['argument']) for r in chain if r['event'].startswith('prepend-')]
    if prepends[:3] != [('prepend-action', 0), ('prepend-event', 0xd0144), ('prepend-event', 0xd0144)]:
        raise ValueError('recovery task prefix differs')
    stop = next(r for r in chain if r['event'] == 'stop-end')
    p = stop['path']
    if p['retry'] != [0, 0] or p['counts'] != [0, 0] or p['indices'] != EMPTY or p['links'] != [0, 0]:
        raise ValueError('stopped path retained work')
    if p['destination'] != [0xc7fa0000, 0xc7fa0000] or not p['flags'] & 0x100000:
        raise ValueError('path invalidation differs')
    final = [r for r in rows if r.get('c') == 1376 and r['event'] in
             ('arrival-cleanup-begin', 'arrival-cleanup-end', 'stop-begin', 'stop-end',
              'dispatch-tasks-begin', 'dispatch-tasks-end', 'pop-task-begin', 'pop-task-end')]
    arrival = [r for r in final if r['event'].startswith('arrival-cleanup')]
    if len(arrival) != 2 or arrival[0]['unit']['count'] != 1:
        raise ValueError('missing successor arrival')
    finish = arrival[1]['unit']
    if finish['count'] != 0 or any(finish[k] != EMPTY for k in ('tasks', 'orders', 'tail')):
        raise ValueError('successor did not unwind both queues')
    if arrival[0]['unit']['refs'] != finish['refs']:
        raise ValueError('successor cleanup leaked references')
    searches = [r for r in rows if r['event'] == 'fine-end']
    if len(searches) != 4 or [r['c'] for r in searches] != [1191, 1283, 1318, 1320]:
        raise ValueError('incomplete retry and successor search lifetime')
    return dict(recovery=chain, successor=final, searches=searches)


def verify(expected, archive):
    public, observed = [], []
    for name, pin in expected['captures'].items():
        raw = (archive / name).read_bytes()
        if len(raw) != pin['bytes'] or digest(raw) != pin['sha256']:
            raise ValueError('capture pin differs')
        rows = [json.loads(l) for l in raw.splitlines()]
        meta = {k: v for k, v in rows[0].items() if k != 'pid'}
        if meta != pin['metadata'] or meta['sha256'] != expected['binary_sha256'] or not meta['owned']:
            raise ValueError('capture provenance differs')
        if any(r.get('type') == 'error' or r['event'] == 'trace-failed' for r in rows):
            raise ValueError('failed capture')
        preload = (archive / (Path(name).stem + '-preload.txt')).read_bytes()
        footers = [r for r in rows if r['event'] == 'preload-file']
        if len(footers) != 1 or not footers[0]['complete'] or digest(preload) != footers[0]['sha256']:
            raise ValueError('preload pin differs')
        markers = re.findall(r'call Preload\( "(R197 [^"\r\n]+)" \)', preload.decode())
        if len(markers) != 253 or footers[0]['markers'] != 253:
            raise ValueError('incomplete public lifetime')
        public.append(markers)
        inputs = [r for r in rows if r['event'] == 'player-input']
        if len(inputs) != 1 or inputs[0]['rc'] != 0 or not inputs[0]['plan']['shift']:
            raise ValueError('missing genuine Shift input')
        if meta['mode'] == 'observe':
            if [r['value'] for r in rows if r['event'] == 'marker'][2:] != markers:
                raise ValueError('observer differs from public preload')
            published = [r for r in rows if r['event'] == 'publish-order']
            if len(published) != 1 or published[0]['flags'] != 1 or published[0]['order'] != 851986:
                raise ValueError('Move was not appended through real player admission')
            if published[0]['point'] != [1142403253, 1145338370] or not 1190 < published[0]['c'] < 1319:
                raise ValueError('wrong successor request')
            finish = [r for r in rows if r['event'] == 'trace-end']
            if len(finish) != 1 or finish[0]['installed'] is not True:
                raise ValueError('observer not completed')
            observed.append(recovery(rows))
        elif meta['mode'] != 'control' or any(r['event'] not in
                ('metadata', 'loading-key', 'control-start-file', 'player-input', 'preload-file') for r in rows):
            raise ValueError('control contains instrumentation')
    if len(observed) != 2 or observed[0] != observed[1] or observed[0] != expected['stages']:
        raise ValueError('original recovery and successor streams differ')
    if len(public) != 3 or any(p != public[0] for p in public) or public[0] != expected['public_markers']:
        raise ValueError('observer-free public continuation differs')
    for name, pin in expected['sources'].items():
        if digest((ROOT / name).read_bytes()) != pin:
            raise ValueError('source pin differs: ' + name)
    if digest((archive / 'RS-Recovery197-v2.w3m').read_bytes()) != expected['map_sha256']:
        raise ValueError('map pin differs')
    return dict(passed=True, status='live-blocked-recovery-successor', observations=2, controls=1,
                public_markers=253, recovery_counter=1319, successor_counter=1376,
                searches=4, recovery_rows=len(observed[0]['recovery']), binary_sha256=expected['binary_sha256'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('expected', 'archive', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        p.error('report must be fresh')
    report = verify(json.loads(gzip.decompress(a.expected.read_bytes())), a.archive)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
