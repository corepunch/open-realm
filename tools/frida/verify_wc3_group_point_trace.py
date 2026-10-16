#!/usr/bin/env python3
"""Check public CGroup point admission, retaining the shared-request boundary."""
import argparse
import hashlib
import json
import re
from pathlib import Path

FORMS = ('name', 'id', 'loc', 'idloc')
TICKS = (10, 80, 150, 220)
EVENTS = ('group-point-native-begin', 'group-point-request-begin',
          'group-point-member-begin', 'group-point-member-end',
          'group-point-request-end', 'group-point-native-end')


def admissions(rows):
    result, batch, members = [], None, None
    for row in rows:
        event = row.get('event')
        if event not in EVENTS:
            continue
        if event == 'group-point-native-begin':
            if batch is not None:
                raise ValueError('nested or incomplete group point request')
            batch = []
        if batch is None:
            raise ValueError('group point event outside public native')
        clean = {k: v for k, v in row.items() if k not in ('ms', 'handle', 'group', 'context', 'request', 'unit')}
        if event.startswith('group-point-member'):
            if members is None:
                members = []
            if row['unit'] not in members:
                members.append(row['unit'])
            clean['member'] = members.index(row['unit'])
        batch.append(clean)
        if event == 'group-point-native-end':
            result.append(batch)
            batch = None
    if batch is not None or len(result) != 4 or len(members or []) != 12:
        raise ValueError('public point request/member count differs')
    return result


def verify(rows, fixture):
    metadata = [r for r in rows if r.get('event') == 'metadata']
    ends = [r for r in rows if r.get('event') == 'trace-end']
    if len(metadata) != 1 or len(ends) != 1 or any(r.get('type') == 'error' for r in rows):
        raise ValueError('missing metadata/end or observer error')
    actual = {k: v for k, v in metadata[0].items() if k not in ('event', 'pid')}
    if actual != fixture['metadata']:
        raise ValueError('group point provenance/configuration differs')
    markers = [r['value'] for r in rows if r.get('event') == 'marker']
    if not any('label=start_group_orders ' in s for s in markers) or not any('tick=300 label=complete ' in s for s in markers):
        raise ValueError('incomplete group point scene')
    normalized = admissions(rows)
    if normalized != fixture['admissions']:
        raise ValueError('group point phases, members or scalar words differ')
    member_rows = [r for r in rows if r.get('event') == 'group-point-member-begin']
    for n in range(4):
        batch = member_rows[n*24:(n+1)*24]
        if [r['phase'] for r in batch] != ['attach']*12 + ['admit']*12:
            raise ValueError('all-member attach must precede admission')
        if len({r['request'] for r in batch}) != 1 or batch[0]['request'] == '0x0':
            raise ValueError('members must retain one live shared move request')
        if [r['unit'] for r in batch[:12]] != [r['unit'] for r in batch[12:]]:
            raise ValueError('admission snapshot order differs')
    group_markers = [r['value'] for r in rows if r.get('event') == 'group-order-marker']
    roster = []
    for tick, form in zip(TICKS, FORMS):
        if group_markers.count(f'PATHGROUP tick={tick} form={form} accepted=1') != 1:
            raise ValueError('public form acceptance differs')
        parsed = []
        for text in group_markers:
            match = re.fullmatch(rf'PATHGROUP tick={tick} member=(\d+) handle=(\d+) order=(\d+) x=(-?[\d.]+) y=(-?[\d.]+)', text)
            if match:
                i, handle, order = map(int, match.groups()[:3])
                parsed.append([i, handle, order])
        if [p[0] for p in parsed] != list(range(14)) or [p[2] for p in parsed] != [851986]*12+[0]*2:
            raise ValueError('native twelve-member cap or insertion order differs')
        roster.append([p[1] for p in parsed])
    if any(r != roster[0] for r in roster) or len(set(roster[0])) != 14:
        raise ValueError('group public roster changed')
    digest = hashlib.sha256(json.dumps(normalized, separators=(',', ':')).encode()).hexdigest()
    return dict(passed=True, public_forms=4, admitted_members=48, excluded_members=8,
                shared_requests=4, all_attach_before_admit=True, admission_sha256=digest,
                scope='Public point admission only; retained shared request observed. Complete group movement/velocity trajectory excluded: this capture deliberately caps motion rows.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--repeat', type=Path)
    parser.add_argument('--fixture', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    fixture = json.loads(args.fixture.read_text())
    read = lambda p: [json.loads(s) for s in p.read_text().splitlines()]
    result = verify(read(args.trace), fixture)
    if args.repeat:
        if verify(read(args.repeat), fixture) != result:
            raise ValueError('group point admission repeat differs')
        result['repeated'] = True
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
