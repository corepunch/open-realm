"""Export Stop/replacement expectations exclusively from original retail captures."""
import json
import gzip
from pathlib import Path


def extract(path):
    path = Path(path)
    raw = gzip.decompress(path.read_bytes()).decode() if path.suffix == '.gz' else path.read_text()
    rows = [json.loads(line) for line in raw.splitlines()]
    bindings = [row['handle'] for row in rows if row['event'] == 'unit-resolve']
    if len(bindings) != 8 or len(set(bindings)) != 8:
        raise ValueError('missing public creation bindings')
    roles = {handle: i for i, handle in enumerate(bindings)}
    movers = {}
    for row in rows:
        if row['event'] == 'marker':
            for unit in row['units']:
                movers[unit['mover']] = roles[unit['handle']]
    visual, before = [[] for _ in range(8)], None
    for row in rows:
        if row['event'] == 'visual-begin':
            if before is not None:
                raise ValueError('nested/missing visual return')
            before = row
        elif row['event'] == 'visual-end':
            if before is None or before['mover'] != row['mover']:
                raise ValueError('unpaired visual visit')
            visual[movers[row['mover']]].append([before['facing'], *before['visual'], *row['visual']])
            before = None
    if before is not None or sum(map(len, visual)) != 326:
        raise ValueError('incomplete visual stream')
    snapshots = {}
    for label in ('after_cancel_angular', 'complete'):
        matches = [r for r in rows if r['event'] == 'marker' and ' label='+label+' ' in r['value']]
        if len(matches) != 1 or len(matches[0]['units']) != 8:
            raise ValueError('missing cancellation boundary')
        samples = sorted(matches[0]['units'], key=lambda u: roles[u['handle']])
        snapshots[label] = [[*u['position'], *u['velocity'], u['facing'], *u['visual'],
                            int(u['groupIdentity'] != [0xffffffff, 0xffffffff])] for u in samples]
    samples = [r for r in rows if r['event'] == 'marker' and ' label=sample ' in r['value']]
    if [r['tick'] for r in samples] != list(range(1, 81)):
        raise ValueError('incomplete public sample timeline')
    snapshots['samples'] = [[*u['position'], *u['velocity'], u['facing'], *u['visual'],
                             int(u['groupIdentity'] != [0xffffffff, 0xffffffff])]
                            for r in samples for u in sorted(r['units'], key=lambda u: roles[u['handle']])]
    stops, stack = [], []
    for row in rows:
        if row['event'] == 'stop-begin':
            stack.append(row)
        elif row['event'] == 'stop-end':
            if not stack or stack[-1]['mover'] != row['mover']:
                raise ValueError('unpaired Stop')
            before = stack.pop()
            def state(r):
                p = r['ownedPath']
                return [*r['position'], *r['velocity'], r['facing'], *r['visual'],
                        int(r['groupIdentity'] != [0xffffffff, 0xffffffff]), p['flags'],
                        *p['counts'], *p['indices'], *p['destination'],
                        *[int(v != 0) for v in p['links']], p['delay'], p['retry']]
            stops.append([before['tick'], movers[row['mover']], *before['arguments'][:5],
                          *state(before), *state(row)])
    if stack or len(stops) != 30:
        raise ValueError('incomplete physical cancellation stream')
    return dict(visual=visual, snapshots=snapshots, stops=stops)


def render(path):
    data = extract(path)
    array = lambda name, rows: ('static uint32_t const '+name+'[]['+str(len(rows[0]))+'] = {\n'+
        ''.join('    {'+','.join('0x%08xu' % word for word in row)+'},\n' for row in rows)+'};\n')
    return ('/* Original Cancel208 physical cancellation and independent visual settling. */\n'+
            ''.join(array('cancel208_visual%d' % i, rows) for i, rows in enumerate(data['visual']) if rows)+
            ''.join(array('cancel208_'+name, rows) for name, rows in data['snapshots'].items()))


def scene(path):
    source = Path(path).read_text()
    source += 'function main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n'
    return ('/* Gameplay and observation boundaries from cancel208_probe.j; test main calls PathProbeInit. */\n'
            'static char const cancel208_scene[] =\n'+
            ''.join('    '+json.dumps(line+'\n')+'\n' for line in source.splitlines())+';\n')
