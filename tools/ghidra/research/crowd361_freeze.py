#!/usr/bin/env python3
"""Extract literal natural36-follower decisions from TARGET-02.1 read-only captures."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path


def extract(path):
    source = path.read_bytes()
    records = [json.loads(line) for line in source.decode().splitlines()]
    selves = []
    pending = {}
    output = []
    active = False
    ended = False
    for row in records:
        event = row.get('event')
        if event == 'marker' and ' s=0 ' in row['value']:
            if ' label=begin-setup' in row['value']:
                active = True
            if ' label=end-cleanup' in row['value']:
                active = False
                ended = True
        if not active:
            continue
        if event == 'begin-target' and row['self'] not in selves:
            selves.append(row['self'])
        if event == 'gtick' and len(row.get('members', [])) == 1 and row['members'][0].get('id') in selves:
            pending[(row['c'], row['g'])] = row
        if event == 'gtick-end' and (row['c'], row['g']) in pending:
            before = pending.pop((row['c'], row['g']))
            # The lite observer omits positive-countdown sample rows. Every
            # actual visible owner visit decrements or reloads the countdown;
            # a group pruned before sampling retains the old value.
            if before['cd'] == row['cd']:
                continue
            member = before['members'][0]
            route = member['path']
            group = before['path']
            words = [before['c'], selves.index(member['id']), before['cd'], before['unseen'],
                     *member['pos'], *route['times'], *route['dest'], *group['dest'],
                     *group['times'], *group['cnt'], *group['idx'], *route['cnt'], *route['idx']]
            output.append([word & 0xffffffff for word in words])
    if not ended or len(selves) != 36 or len(output) != 22772:
        raise ValueError('complete36-follower natural owner stream required')
    return dict(sha256=hashlib.sha256(source).hexdigest(), rows=output)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', type=Path, nargs=2)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--header', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists() or args.header.exists():
        ap.error('fresh outputs required')
    runs = [extract(path) for path in args.captures]
    if runs[0]['rows'] != runs[1]['rows']:
        raise ValueError('original natural owner streams differ')
    args.output.write_bytes(gzip.compress((json.dumps(runs, separators=(',', ':'))+'\n').encode(), mtime=0))
    args.header.write_text('/* Literal read-only TARGET-02.1 crowd C1/C2 owner snapshots.\n'
                          ' * Source hashes: '+runs[0]['sha256']+' / '+runs[1]['sha256']+'. */\n'
                          'static uint32_t const crowd361_rows[][22]={\n'+
                          ''.join('{'+','.join(map(str,row))+'},\n' for row in runs[0]['rows'])+'};\n')
    print(len(runs[0]['rows']))


if __name__ == '__main__':
    main()
