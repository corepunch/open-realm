#!/usr/bin/env python3
"""Freeze expected-BASE-02.1.json from the oracle report and a checked live capture."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify_base021_types_trace as chk  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--oracle', type=Path, required=True)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--repeat', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    oracle = json.loads(args.oracle.read_text())
    rows, _ = chk.load(args.capture)
    norm = chk.normalize(rows)
    rows2, _ = chk.load(args.repeat)
    norm2 = chk.normalize(rows2)
    assert norm['events'] == norm2['events'] and norm['birth'] == norm2['birth']
    parse = {p['text']: p['bits'] for p in oracle['parse']}
    lanes = {l['bits']: l for l in oracle['lanes']}
    births = []
    for b in norm['birth']:
        text = chk.UMVT.get(b['type'], chk.STOCK.get(b['type']))
        births.append(dict(index=b['index'], type=b['type'], movetp=text, bits=parse[text], class_flags=b['class_flags'],
                           published_class=b['class_flags'] >> 1, category=b['profile'][0], query=b['profile'][1],
                           adaptive=b['adaptive']))
    runtime = [e for e in norm['events'] if e[0] in ('producer', 'publish-profile', 'publish-class', 'set-adaptive')
               or (e[0] == 'stock-marker' and 'label=state' not in e[1] and 'label=created' not in e[1])]
    support = {}
    for k, v in norm['support'].items():
        states = []
        for row in v:
            if not states or states[-1][1:] != row[1:]:
                states.append(row)
        support[str(k)] = dict(first=v[0], last=v[-1], state_changes=states)
    stock = [e[1] for e in norm['events'] if e[0] == 'stock-marker']
    expected = dict(version=1, task='BASE-02.1',
                    game_sha256=oracle['game_sha256'], storm_sha256=oracle['storm_sha256'], crt_sha256=oracle['crt_sha256'],
                    oracle=dict(name_table=[[t['name'], t['bits']] for t in oracle['name_table']], parse=parse,
                                nonzero_lanes={f'{k:02x}': dict(query=l['map_edx_0'], category=l['map_edx_1'],
                                                                category_any_nonzero_edx=[l['map_edx_2'], l['map_edx_80000000'], l['map_edx_ffffffff']],
                                                                class_flags=l['class_flags'], published_class=l['class_flags'] >> 1)
                                               for k, l in lanes.items() if l['map_edx_0'] or l['map_edx_1'] or l['class_flags']},
                                all_other_inputs='query 0, category 0, class flags 0 (0..7f and 80,81,ff,100,7fffffff,80000000,ffffffff)',
                                adaptive_lane_mask_table_6fce4570=['06000006', '80000080', '40000040', '04000004'],
                                adaptive_lane_shift='2*((path+88>>30)&3) = 2*published_class'),
                    live=dict(capture_sha256=hashlib.sha256(args.capture.read_bytes()).hexdigest(),
                              repeat_sha256=hashlib.sha256(args.repeat.read_bytes()).hexdigest(),
                              births=births, runtime_events=runtime, stock_markers=stock, support=support))
    text = json.dumps(expected, indent=1) + '\n'
    args.output.write_text(text)
    print(hashlib.sha256(text.encode()).hexdigest(), len(runtime), len(stock))


if __name__ == '__main__':
    main()
