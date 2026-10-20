#!/usr/bin/env python3
"""Compare a retreat473 retail UI save/load capture's post-load pass with its first pass.

The first pass runs ticks 1..300 uninterrupted after the native UI save (it is its own
control for the suffix). The load restores the saved tick S; the second pass runs S+1..300.
Observed captures: every public marker with its raw getter words, every Captain policy
record and every readable Captain snapshot after the resume must equal the first pass from
tick S+1 (process addresses, host time and sequence numbers excluded). Native Captain objects
are reconstructed by load, so snapshots through stale pre-load pointers fault until a policy
producer reports the new object; snapshots are matched by authored home where both passes
read a live object. Observer-free controls: the Preload marker stream's second segment must
equal its first pass suffix. With an accepted uninterrupted fixture, the first pass must equal
its public rows (observed) or the accepted marker stream (control).
"""
import argparse, json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import retreat473_expected as expected  # noqa: E402

UI = {'ui-action', 'ui-focus', 'paused', 'save-done', 'load-resumed', 'phase-end', 'save-file'}
TICK = re.compile(r'tick=(\d+) ')


def passes(rows):
    """Split semantic records at the tick reset caused by the load."""
    sem = [{k: v for k, v in r.items() if k not in ('seq', 'ms', 'elapsed')}
           for r in rows if r.get('event') not in expected.TRANSPORT | UI]
    ticks = [(i, int(TICK.search(r['value'])[1])) for i, r in enumerate(sem) if r.get('event') == 'marker']
    resets = [(i, t) for (_, p), (i, t) in zip(ticks, ticks[1:]) if t < p]
    if len(resets) != 1:
        raise ValueError('exactly one load reset required')
    reset = resets[0]
    # Each pass ends at its own final public marker; later wall-clock tail records
    # (periodic updates while the harness waits) are not part of either pass.
    tail = lambda x: x[:max(i for i, r in enumerate(x) if r.get('event') == 'marker') + 1]
    return tail(sem[:reset[0]]), tail(sem[reset[0]:]), reset[1]


def strip(records, start_tick):
    out, keep, alias = [], False, {}

    def a(v):
        if not isinstance(v, str) or v == '0x0' or not v.startswith('0x'):
            return v
        return alias.setdefault(v, f'P{len(alias)}')
    for r in records:
        if r.get('event') == 'marker':
            keep = int(TICK.search(r['value'])[1]) >= start_tick
        if not keep:
            continue
        r = dict(r)
        for key in ('captain', 'unit', 'target'):
            if key in r and isinstance(r[key], str):
                r[key] = a(r[key])
        for key in ('before', 'after'):
            if isinstance(r.get(key), dict):
                r[key] = {**r[key], 'captain': a(r[key]['captain'])}
        if 'captains' in r:
            r['captains'] = [{**c, 'captain': a(c['captain'])} for c in r['captains']]
        if isinstance(r.get('captain'), dict):
            r['captain'] = {**r['captain'], 'captain': a(r['captain']['captain'])}
        out.append(r)
    return out


def public_rows(records):
    rows, pending = [], None
    for r in records:
        if r.get('event') == 'public-position':
            pending = r
        elif r.get('event') == 'marker' and 'label=unit' in r['value']:
            tick, u, order = map(int, re.match(r'R473 tick=(\d+) label=unit u=(\d+) order=(\d+) ', r['value']).groups())
            if pending is None or pending['u'] != u or pending['tick'] != tick:
                raise ValueError('getter/marker pairing')
            rows.append([u, tick, order, pending['xy'][0], pending['xy'][1]])
            pending = None
    return rows


def compare(rows, accepted_public=None):
    first, second, resumed = passes(rows)
    one, two = strip(first, resumed), strip(second, resumed)
    pub = lambda x: [{k: v for k, v in r.items() if k != 'captains'} for r in x if r.get('event') in ('marker', 'public-position')]
    public_one, public_two = pub(one), pub(two)
    snaps = [(x.get('tick'), [c for c in x.get('captains', []) if 'error' not in c], [c for c in y.get('captains', []) if 'error' not in c])
             for x, y in zip(one, two) if x.get('event') == 'marker' and 'label=unit' not in x['value']]
    byhome = lambda cs: {tuple(c['home']): {k: v for k, v in c.items() if k != 'captain'} for c in cs}
    live = []
    for t, sa, sb in snaps:
        x, y = byhome(sa), byhome(sb)
        live += [(t, home, x[home], y[home]) for home in sorted(set(x) & set(y))]
    snapshot_diffs = [(t, list(home)) for t, home, x, y in live if x != y]
    calls_one = [r for r in one if r.get('event') not in ('marker', 'public-position')]
    calls_two = [r for r in two if r.get('event') not in ('marker', 'public-position')]
    strip_ptr = lambda r: {k: v for k, v in r.items() if k not in ('captain', 'unit', 'target', 'before', 'after')} | \
        {k: {kk: vv for kk, vv in r[k].items() if kk != 'captain'} for k in ('before', 'after') if isinstance(r.get(k), dict)}
    call_diffs = sum(1 for x, y in zip(calls_one, calls_two) if strip_ptr(x) != strip_ptr(y)) + abs(len(calls_one) - len(calls_two))
    public_diffs = sum(1 for x, y in zip(public_one, public_two) if x != y) + abs(len(public_one) - len(public_two))
    saved = [r for r in first if r.get('event') == 'marker' and 'label=unit' not in r['value'] and
             int(TICK.search(r['value'])[1]) == resumed - 1]
    attack_at_save = [c for r in saved for c in r.get('captains', []) if c.get('home') == expected.HOME]
    report = dict(resumed_tick=resumed, saved_flags=attack_at_save[0]['flags'] if attack_at_save else None,
                  saved_actor_velocity=(attack_at_save[0].get('actor') or {}).get('velocity') if attack_at_save else None,
                  public_records=len(public_one), public_differences=public_diffs,
                  captain_snapshots_compared=len(live), attack_snapshots_compared=sum(1 for t, h, x, y in live if list(h) == expected.HOME),
                  captain_snapshots_unreadable=sum(1 for t, sa, sb in snaps if not sa or not sb),
                  captain_snapshot_differences=len(snapshot_diffs), snapshot_difference_ticks=snapshot_diffs[:20],
                  policy_records=len(calls_one), policy_record_differences=call_diffs)
    if accepted_public is not None:
        rows_first = public_rows(first)
        report['first_pass_public_rows'] = len(rows_first)
        report['first_pass_equals_accepted'] = rows_first == accepted_public
    return report


def compare_control(preload_text, accepted_markers=None):
    markers, _ = expected.preload_markers(preload_text)
    ticks = [int(TICK.search(m)[1]) for m in markers]
    resets = [i for i in range(1, len(ticks)) if ticks[i] < ticks[i - 1]]
    if len(resets) != 1:
        raise ValueError('exactly one load reset required')
    first, second = markers[:resets[0]], markers[resets[0]:]
    resumed = ticks[resets[0]]
    suffix = [m for m in first if int(TICK.search(m)[1]) >= resumed]
    report = dict(resumed_tick=resumed, first_markers=len(first), second_markers=len(second),
                  second_equals_first_suffix=second == suffix)
    if accepted_markers is not None:
        report['first_pass_equals_accepted'] = first == accepted_markers
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', required=True)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--expected', type=Path, help='accepted uninterrupted retreat473 fixture JSON')
    ap.add_argument('--accepted-preload', type=Path, help='accepted uninterrupted Preload file (control comparison)')
    a = ap.parse_args()
    rows = [json.loads(l) for l in open(a.capture)]
    if rows[0].get('mode') == 'control':
        pre = Path(str(Path(a.capture).with_suffix('')) + '-preload.txt').read_text(encoding='utf-8')
        acc = expected.preload_markers(a.accepted_preload.read_text(encoding='utf-8'))[0] if a.accepted_preload else None
        report = compare_control(pre, acc)
    else:
        report = compare(rows, json.loads(a.expected.read_text())['public'] if a.expected else None)
    report = dict(capture=a.capture, **report)
    a.report.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
