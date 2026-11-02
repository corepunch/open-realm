#!/usr/bin/env python3
"""Freeze expected-NUM-04.6.json: CRandData 45-stream words and ownership.

Inputs: the NUM-04.6 Unicorn oracle fixture (verify_NUM-04.6_streams.py), the NUM-04.5 capture analysis (every live
stream draw, model-checked), the static stream call-site/owner table and the trace-end aggregates of the NUM-04.5
captures (non-owner, non-stream generator states: audio, sprite animation, particle/lightning locals).
"""
import argparse, hashlib, json
from collections import Counter, defaultdict
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--oracle', type=Path, required=True)
    ap.add_argument('--analysis', type=Path, required=True)
    ap.add_argument('--owners', type=Path, required=True)
    ap.add_argument('--captures', type=Path, nargs='+', required=True)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    oracle = json.loads(a.oracle.read_text()); an = json.loads(a.analysis.read_text()); owners = json.loads(a.owners.read_text())
    live = {}
    for name, c in an['captures'].items():
        per = defaultdict(list)
        for t in c['timeline']:
            if t['what'] == 'draw' and t['cls'].startswith('stream:'):
                per[t['cls']].append(dict(caller=t['caller'], before=t['before'], after=t['after'], value=t['value'], visit=t['visit']))
        live[name] = dict(game_seed=c['game_seed'], locked=c['locked'], checks={k: v for k, v in c['checks'].items() if 'stream' in k or 'reseed' in k},
                          stream_draws={k: v for k, v in sorted(per.items())},
                          reseeds=[t for t in c['timeline'] if t['what'] in ('streams-reseed', 'SetRandomSeed')])
    other = {}
    for d in a.captures:
        rows = [json.loads(l) for l in open(d / 'capture.jsonl')]
        end = next((r for r in rows if r.get('event') == 'trace-end'), None)
        if not end:
            continue
        other[d.name] = {k: v for k, v in sorted(end['agg'].items()) if not k.startswith(('owner@', 'stream'))}
    out = dict(task='NUM-04.6', binary_sha256=oracle['binary_sha256'], oracle_sha256=hashlib.sha256(a.oracle.read_bytes()).hexdigest(),
               analysis_sha256=hashlib.sha256(a.analysis.read_bytes()).hexdigest(),
               layout=dict(registry='TlsSlots_Get(0xd) -> [+0x10] data array, entry 3', object='CRandData 0x16c bytes', vtable=0x6fb7a69c,
                           stream_offset='4 + 8*index', count=45, state='WC3PathRandomState {sum, index}'),
               oracle=dict(reseed=oracle['reseed'], set_random_seed=oracle['set_random_seed'], consumers=oracle['consumers'], stand_ins=oracle['stand_ins']),
               static_owners=owners, live=live, other_generators=other)
    a.out.write_text(json.dumps(out, indent=1) + '\n')
    print(hashlib.sha256(a.out.read_bytes()).hexdigest(), a.out)


if __name__ == '__main__':
    main()
