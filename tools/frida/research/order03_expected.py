#!/usr/bin/env python3
"""Freeze expected-ORDER-03.1.json / expected-ORDER-03.2.json from the oracle report and the live captures.

  order03_expected.py <research root> ORDER-03.1|ORDER-03.2 ... [--check]

Live part: observer-free control marker lists (exact strings) per variant, observer==control
marker equality, normalized observer event rows per phase (order03_analyze.summary) and the
repeat comparison of the two forward observer runs. Physical CUnit destructor rows (6790f0) are
excluded from the frozen rows: their position relative to the final JASS marker differs between
repeats (reported separately). Oracle part: named case results of the matching group.
"""
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import order03_analyze as A  # noqa: E402

TICKS = {'ORDER-03.1': {0, 5, 10, 15, 20, 50, 53}, 'ORDER-03.2': {25, 28, 32, 36, 37, 40, 45}}
FIRST_PROBE_CAPTURES = ['forward-observe-1.jsonl', 'forward-observe-1-rs-o3-forward.txt', 'forward-observe-2.jsonl',
                        'forward-observe-2-rs-o3-forward.txt', 'forward-control-1.jsonl', 'forward-control-1-rs-o3-forward.txt',
                        'reverse-observe-1.jsonl', 'reverse-observe-1-rs-o3-reverse.txt', 'reverse-control-1.jsonl',
                        'reverse-control-1-rs-o3-reverse.txt']
GROUP = {'ORDER-03.1': '03.1', 'ORDER-03.2': '03.2'}
PHASES = {
    'ORDER-03.1': {'1': 'baseline delivery to T1,T2,(P1),T3,T4', '2': 'T1 inserts T5 and destroys T3; T2 destroys itself; T4 destroys T1',
                   '3': 'next dispatch after mutation', '10': 'G1 registers 21 events during dispatch (deferred table growth)',
                   '11': 'dispatch after growth'},
    'ORDER-03.2': {'4': 'N1 inserts N4 then nested IssuePointOrder; inner N1 destroys N3', '5': 'dispatch after nesting',
                   '6': 'R1 issues Stop (current order replaced, nested immediate-order event RI) before R2 reads the payload',
                   '7': 'K1 RemoveUnit inside the unit dispatch; K2/K3 still delivered; removal completes after the JASS callback',
                   '8': 'L1 KillUnit inside the dispatch: nested death event LD at depth 3, then L2/L3', '9': 'RemoveUnit of the dead unit outside dispatch'},
}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def tick_of(value):
    import re
    m = re.search(r'tick=(\d+)', value)
    return int(m.group(1)) if m else None


def rows_by_tick(rows, ticks):
    out, cur = [], 0
    for r in rows:
        if r[0] == 'marker':
            cur = tick_of(r[1])
        if cur in ticks and not (r[0] == 'destructor' and r[1] == 'CUnit'):
            out.append([cur] + r)
    return out


def build(root, task):
    cap = root / 'ORDER-03.1' / 'captures'
    oracle = json.loads((root / 'ORDER-03.1' / 'oracle-report-ORDER-03.1.json').read_text())
    ticks = TICKS[task]
    live = {'captures': {}, 'controls': {}, 'observer_equals_control': {}, 'repeat': {}}
    for name in FIRST_PROBE_CAPTURES:
        live['captures'][name] = sha(cap / name)
    for variant in ('forward', 'reverse'):
        control = cap / ('%s-control-1-rs-o3-%s.txt' % (variant, variant))
        markers = A.markers(str(control))
        live['controls'][variant] = [m for m in markers if tick_of(m) in ticks]
        for run in sorted(cap.glob('%s-observe-*.jsonl' % variant)):
            live['observer_equals_control'][run.name] = A.markers(str(run)) == markers
    s1 = A.summary(str(cap / 'forward-observe-1.jsonl'))
    s2 = A.summary(str(cap / 'forward-observe-2.jsonl'))
    strip = lambda s: [r for r in s if not (r[0] == 'destructor' and r[1] == 'CUnit')]
    live['repeat'] = {'forward-observe-1 vs 2 (CUnit destructor rows excluded)': strip(s1) == strip(s2),
                      'cunit_destructor_rows': {'forward-observe-1': [r for r in s1 if r[0] == 'destructor' and r[1] == 'CUnit'],
                                                'forward-observe-2': [r for r in s2 if r[0] == 'destructor' and r[1] == 'CUnit']}}
    live['phases'] = PHASES[task]
    live['forward_rows'] = rows_by_tick(s1, ticks)
    live['reverse_rows'] = rows_by_tick(A.summary(str(cap / 'reverse-observe-1.jsonl')), ticks)
    named = [r for r in oracle['named_results'] if r['group'] == GROUP[task]]
    result = {'task': task, 'binary_sha256': oracle['binary_sha256'],
              'oracle': {'script_sha256': oracle['script_sha256'], 'cases': oracle['cases'], 'random': oracle['random'],
                         'seed': oracle['seed'], 'mismatches': oracle['mismatches'], 'stats': oracle['stats'], 'named_results': named},
              'live': live}
    if task == 'ORDER-03.2':
        nested = json.loads((root / 'ORDER-03.2' / 'oracle-report-ORDER-03.2.json').read_text())
        result['oracle']['nested'] = {k: nested[k] for k in ('script_sha256', 'harness_sha256', 'cases', 'random', 'seed', 'mismatches', 'stats')}
        result['oracle']['nested_results'] = nested['named_results']
        cap2 = root / 'ORDER-03.2' / 'captures'
        for probe, units in (('nested', 'nested'), ('payload', 'payload')):
            A.UNIT_BY_TICK.clear()
            A.UNIT_BY_TICK.update(A.UNIT_SETS[units])
            part = {'captures': {}, 'observer_equals_control': {}}
            control = cap2 / ('%s-control-1-rs-o3-%s.txt' % (probe, probe))
            for name in sorted(p.name for p in cap2.glob('%s-*' % probe) if p.suffix in ('.jsonl', '.txt')):
                part['captures'][name] = sha(cap2 / name)
            markers = A.markers(str(control))
            part['control'] = markers
            runs = sorted(cap2.glob('%s-observe-*.jsonl' % probe))
            for run in runs:
                part['observer_equals_control'][run.name] = A.markers(str(run)) == markers
            sums = [A.summary(str(r)) for r in runs]
            strip = lambda s: [r for r in s if not (r[0] == 'destructor' and r[1] == 'CUnit')]
            part['repeat_equal'] = all(strip(x) == strip(sums[0]) for x in sums[1:]) if len(sums) > 1 else None
            part['rows'] = strip(sums[0])
            live[probe] = part
        A.UNIT_BY_TICK.clear()
        A.UNIT_BY_TICK.update(A.UNIT_SETS['first'])
    return result


def main():
    root = Path(sys.argv[1])
    check = '--check' in sys.argv
    for task in [a for a in sys.argv[2:] if a.startswith('ORDER-')]:
        data = json.dumps(build(root, task), indent=1, sort_keys=True) + '\n'
        path = root / task / ('expected-%s.json' % task)
        if check:
            same = path.read_text() == data
            print(task, 'unchanged' if same else 'DIFFERS')
            continue
        if path.exists():
            print(task, 'exists; not overwritten', sha(path))
            continue
        path.write_text(data)
        print(task, path, hashlib.sha256(data.encode()).hexdigest())


if __name__ == '__main__':
    main()
