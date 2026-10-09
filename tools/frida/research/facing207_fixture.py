"""Export original raw timed-owner rows; never use engine output."""
import json
from pathlib import Path


def extract(path):
    rows = [json.loads(line) for line in Path(path).read_text().splitlines()]
    ends = [row for row in rows if row['event'] == 'api-end']
    roles = {row['mover']: i for i, row in enumerate(ends[:5])}
    result, current, commit, canceled = [], None, None, 0
    for row in rows:
        if row['event'] == 'owner-begin':
            current = row if row['flags'] & 0x200 else None
            commit = None
        elif row['event'] == 'commit-end' and current:
            if row['mover'] == current['members'][0]['mover']:
                commit = row
        elif row['event'] == 'owner-end' and current:
            m, after = current['members'][0], row['members']
            if commit is None and not after and row['path'] is None:
                # Canceled cached rows are pruned before a physical decision.
                if (current['counter'] != 1118 or current['tick'] != 28 or
                        roles[m['mover']] != 0 or row['identity'] != [0xffffffff, 0xffffffff]):
                    raise ValueError('unexpected pruned angular visit')
                canceled += 1
                current = None
                continue
            if len(current['members']) != 1 or len(after) > 1 or commit is None:
                raise ValueError('incomplete timed owner/commit')
            a = after[0] if after else commit
            before = [current['counter'], roles[m['mover']], current['flags'], current['age'], current['parameter'],
                      *current['point'], *current['path']['destination'], current['path']['count'], current['path']['index'],
                      *m['position'], *m['velocity'], m['facing'], *m['destination'], m['speed'], m['heading'], m['memberFlags']]
            result.append(before + [*a['position'], *a['velocity'], a['facing'], int(not after)])
            current = None
    if current is not None or len(result) != 174 or canceled != 1:
        raise ValueError('missing timed physical visits')
    return result


def render(path):
    rows = extract(path)
    return ('/* Original Facing207 public timed physical owner words; no engine-generated expectations. */\n'
            'static uint32_t const facing207_owner[][27] = {\n' +
            ''.join('    {' + ','.join('0x%08xu' % word for word in row) + '},\n' for row in rows) + '};\n')


def visual(path):
    rows = [json.loads(line) for line in Path(path).read_text().splitlines()]
    ends = [row for row in rows if row['event'] == 'api-end']
    roles = {row['mover']: i for i, row in enumerate(ends[:5])}
    result = [[] for _ in range(5)]
    before = None
    for row in rows:
        if row['event'] == 'visual-begin':
            before = row
        elif row['event'] == 'visual-end':
            if not before or before['mover'] != row['mover']:
                raise ValueError('incomplete visual visit')
            result[roles[row['mover']]].append([before['facing'], *before['visual'], *row['visual']])
            before = None
    if before or sum(map(len, result)) != 381:
        raise ValueError('missing visual visits')
    return result


def render_visual(path):
    rows = visual(path)
    return ('/* Original Facing207 visual settling words, grouped by independent mover. */\n' +
            ''.join('static uint32_t const facing207_visual%d[][5] = {\n' % i +
                    ''.join('    {' + ','.join('0x%08xu' % word for word in row) + '},\n' for row in values) + '};\n'
                    for i, values in enumerate(rows)))
