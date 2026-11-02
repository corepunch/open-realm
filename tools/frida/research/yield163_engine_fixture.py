#!/usr/bin/env python3
"""Export complete public ROUTE-03/05 member steps; never take engine output."""
import argparse
import hashlib
import json
from pathlib import Path

CASES = ['cross-same', 'cross-diff', 'tunnel-same', 'tunnel-diff', 'tri-converge']
CAPTURES = ['cross-same-observe-1-envB.jsonl', 'cross-diff-observe-1-envB.jsonl',
            'tunnel-same-observe-1-envC.jsonl', 'tunnel-diff-observe-1-envC.jsonl',
            'tri-converge-observe-1-envC.jsonl']
DLL = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'


def render(root, dynamic=False):
    out = ['/* Generated from complete read-only retail ROUTE-03/05 captures. */',
           '/* counter, member, retained pose/velocity/facing, predicted source,',
           ' * next committed velocity/facing, fine count/index, delay/retry, blocker,',
           ' * entry delay/retry, next-facing validity, coarse count/index, request times,',
           ' * and charged fine/member-coarse/group-coarse work. */']
    cases = CASES
    captures = CAPTURES
    prefix = "yield163"
    if dynamic:
        cases = ["after", "before", "peer", "remove_wait", "remove_leg", "remove_far", "terrain", "terrain_remove"]
        captures = ["ukeep-t1430-observe-1-envB.jsonl", "ukeep-t1394-observe-1-envB.jsonl",
                    "peer-t1300-observe-1-envC.jsonl", "uremove-t1430-r1445-observe-1-envB.jsonl",
                    "uremove-t1430-r1659-observe-1-envB.jsonl", "uremove-t1430-r1890-observe-1-envC.jsonl",
                    "tkeep-t1430-observe-1-envB.jsonl", "tremove-t1430-r1445-observe-1-envB.jsonl"]
        prefix = "dynamic163"
    for name, capture in zip(cases, captures):
        path = root/'captures'/capture
        raw = path.read_bytes()
        rows = [json.loads(line) for line in raw.splitlines()]
        assert rows[0]['sha256'] == DLL and rows[0]['owned'] and rows[0]['mode'] == 'observe'
        assert any(r['event'] == 'trace-end' and r['installed'] and r['finished'] for r in rows)
        assert any(r['event'] == 'preload-file' and r['complete'] for r in rows)
        assert not any(r.get('type') == 'error' for r in rows)
        entries = [r for r in rows if r['event'] == 'step-enter']
        movers = sorted({tuple(r['mover']['id']) for r in entries})
        member = {identity: i for i, identity in enumerate(movers)}
        leaves = {(r['counter'], tuple(r['mover']['id'])): r for r in rows if r['event'] == 'step-leave'}
        work = {}; path_buckets = {}; charged = {}
        for row in rows:
            if row['event'] == 'bucket-update':
                work[row['after']['off']] = row['after']['work']
            if row['event'] in ('fine-request', 'acc-request'):
                bucket = row['bucketAfter']; work[bucket['off']] = bucket['work']
                if row['event'] == 'fine-request':
                    path_buckets[row['path']] = bucket['off']
            if row['event'] == 'step-leave':
                fine = path_buckets[row['path']['path']]; player_base = fine - 84
                charged[(row['counter'], tuple(row['mover']['id']))] = [
                    work.get(fine, 0), work.get(player_base+56, 0), work.get(player_base, 0)]
        following = {}
        previous = {}
        for row in entries:
            identity = tuple(row['mover']['id'])
            if identity in previous:
                following[previous[identity]] = row
            previous[identity] = (row['counter'], identity)
        out += [f'/* {capture} SHA256 {hashlib.sha256(raw).hexdigest()} */',
                f'static uint32_t const {prefix}_{name.replace("-", "_")}[][27]={{']
        for row in entries:
            mover = row['mover']; key = (row['counter'], tuple(mover['id']))
            leave = leaves[key]; state = leave['path']; nxt = following.get(key)
            velocity = nxt['mover']['vel'] if nxt else [0, 0]
            facing = nxt['mover']['facing'] if nxt else leave['outputs'][1]
            values = [row['counter'], member[key[1]], *mover['pos'], *mover['vel'], mover['facing'],
                      *row['source'], *velocity, facing, state['fc'], state['fi'], state['delay'],
                      state['retry'], member.get(tuple(state['blk']), 0xffffffff),
                      row['path']['delay'], row['path']['retry'], int(nxt is not None),
                      state['cc'], state['ci'], *state['t'], *charged[key]]
            assert len(values) == 27
            out.append('    {' + ','.join(f'0x{x:08x}u' for x in values) + '},')
        out.append('};')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--research', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    ap.add_argument('--dynamic', action='store_true')
    args = ap.parse_args()
    args.header.write_text(render(args.research, args.dynamic))


if __name__ == '__main__':
    main()
