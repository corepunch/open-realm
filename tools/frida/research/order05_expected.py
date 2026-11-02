#!/usr/bin/env python3
"""Freeze expected-ORDER-05.1/05.2/05.3.json from the request-heap oracle report and the live captures.

  order05_expected.py <research root> ORDER-05.1|ORDER-05.2|ORDER-05.3 ... [--check]

Oracle part: verify_ORDER-05.1_request_heap.py report (one report for all three IDs), named results of the
matching group. Live part (order05 probe, observer order05_observer.js):
  05.1  requests probe phases 1-2 (ticks <= 20): normalized rows, observer==control markers, repeat equality
  05.2  requests probe phases 3-4 (ticks 25..45)
  05.3  wrap probe (300 s request-clock wrap) and UI save/load probe: rebase rows, clock words at markers,
        request save/load rows, pre-load continuation == post-load replay (markers with clock words, in-window rows)
"""
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import order05_analyze as A  # noqa: E402

GROUP = {'ORDER-05.1': '05.1', 'ORDER-05.2': '05.2', 'ORDER-05.3': '05.3'}
TICKS = {'ORDER-05.1': (0, 20), 'ORDER-05.2': (21, 45)}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def tick_of(row):
    return row[1] if isinstance(row[1], int) else int(re.search(r'tick=(\d+)', row[1]).group(1))


def strip_el(markers):
    return [re.sub(r' el=[0-9.]+', '', m) for m in markers]


def requests_part(root, task):
    cap = root / 'ORDER-05.1' / 'captures'
    lo, hi = TICKS[task]
    part = {'captures': {p.name: sha(p) for p in sorted(cap.iterdir()) if p.suffix in ('.jsonl', '.txt', '.log')}}
    control = A.markers(str(cap / 'requests-control-1-rs-o5-requests.txt'))
    part['control_markers'] = [m for m in control if lo <= int(re.search(r'tick=(\d+)', m).group(1)) <= hi]
    runs = ['requests-observe-2.jsonl', 'requests-observe-3.jsonl']
    part['observer_equals_control'] = {r: A.markers(str(cap / r)) == control for r in runs}
    sums = [A.summary(str(cap / r)) for r in runs]
    part['repeat_equal'] = sums[0] == sums[1]
    part['ordercheck'] = {r: A.ordercheck(str(cap / r)) for r in runs}
    part['rows'] = [r for r in sums[0] if lo <= tick_of(r) <= hi]
    return part


def wrap_part(root):
    cap = root / 'ORDER-05.3' / 'captures'
    obs = cap / 'wrap-observe-1.jsonl'
    part = {'captures': {p.name: sha(p) for p in sorted(cap.iterdir()) if p.name.startswith('wrap-') and p.suffix in ('.jsonl', '.txt', '.log')}}
    rows = A.load(str(obs))
    part['ordercheck'] = A.ordercheck(str(obs))
    part['rebase'] = [[r['event'], r['clock']['id'], r['clock']['timeW'], r['clock']['epoch'], r['heap']['count'],
                       [[q['deadlineW'], q['delayW'], q['serial'], q['recvVt'], q['flags']] for q in r['heap']['items']]]
                      for r in rows if r.get('event') in ('rebase-enter', 'rebase-leave')]
    part['near_span_primary'] = [[r['event'], r['clock']['timeW'], r['clock']['epoch'], r.get('incW')] for r in rows
                                 if r.get('event', '').startswith('advance-near-span') and r['clock']['id'] == 'primary']
    part['near_span_presentation'] = [[r['event'], r['clock']['timeW'], r['clock']['epoch'], r.get('incW')] for r in rows
                                      if r.get('event', '').startswith('advance-near-span') and r['clock']['id'] == 'presentation']
    part['enter_markers'] = [[re.sub(r' el=[0-9.]+', '', r['value']), r['clocks']['primary']['timeW'], r['clocks']['primary']['epoch']]
                             for r in rows if r.get('event') == 'marker' and ' enter trig=' in r['value']]
    seq = [r['seq'] for r in rows if r.get('event') == 'rebase-enter' and r['clock']['id'] == 'primary'][0]
    part['around_primary_wrap'] = [[r['event'], r['req']['deadlineW'], r['req']['serial'], r['req']['flags'], r['req']['recvVt']]
                                   for r in rows if r.get('event') in ('execute', 'rearm') and seq - 40 <= r['seq'] <= seq + 30]
    part['clock_flags_events'] = [[r['clock']['id'], r['before'], r['clock']['flags']] for r in rows if r.get('event') == 'clock-flags']
    observed = A.markers(str(obs))
    part['observer_markers'] = len(observed)
    ctl = cap / 'wrap-control-1-rs-o5-wrap.txt'
    if ctl.exists():
        part['observer_equals_control'] = A.markers(str(ctl)) == observed
    return part


def saveload_part(root):
    cap = root / 'ORDER-05.3' / 'captures'
    obs = cap / 'saveload-observe-1.jsonl'
    part = {'captures': {p.name: sha(p) for p in sorted(cap.iterdir()) if p.name.startswith('saveload-') and p.suffix in ('.jsonl', '.txt', '.log', '.png')}}
    rows = A.load(str(obs))
    part['ui_actions'] = [[r['at'], r['kind'], r['value'], r['status']] for r in rows if r.get('event') == 'ui-action']
    part['request_save'] = [[r['tick'], r['req']['deadlineW'], r['req']['delayW'], r['req']['serial'], r['req']['flags'], r['req']['recvVt'],
                             r['clocks']['primary']['timeW'], r['clocks']['primary']['serial']] for r in rows if r.get('event') == 'request-save']
    part['wrapper_load'] = [[r['wrapper']['vt'], r['timer'] and [r['timer']['deadlineW'], r['timer']['delayW'], r['timer']['serial'], r['timer']['flags']],
                             r['release'] and [r['release']['deadlineW'], r['release']['delayW'], r['release']['serial'], r['release']['flags']]]
                            for r in rows if r.get('event') == 'wrapper-load']
    part['load_clock'] = [[r['event'], r['clocks'].get('primary'), r['clocks'].get('presentation')] for r in rows
                          if r.get('event') in ('game-load-enter', 'game-load-leave')]
    li = [i for i, r in enumerate(rows) if r.get('event') == 'game-load-leave'][0]

    def marks(rs):
        return [(r['value'], r['clocks']['primary']['timeW'], r['clocks']['primary']['serial']) for r in rs if r.get('event') == 'marker']
    pre, post = marks(rows[:li]), marks(rows[li:])
    start = [i for i, m in enumerate(pre) if m[0] == post[0][0]][0]
    seg = pre[start:]
    n = min(len(seg), len(post))
    part['post_load_first_marker'] = post[0]
    part['pre_continuation_vs_post_load_markers'] = {'compared': n, 'equal': seg[:n] == post[:n]}

    def window_rows(rs, lo, hi):
        out, tick, win = [], 0, False
        for r in rs:
            if r.get('event') == 'marker':
                tick = int(re.search(r'tick=(\d+)', r['value']).group(1))
                win = True if ' win=1' in r['value'] else (False if ' win=0' in r['value'] else win)
            if win and lo <= tick < hi and r.get('event') in ('execute', 'queue', 'rearm', 'range-emit-enter', 'release', 'stop-timer', 'start-timer'):
                q = r.get('req')
                out.append([r['event'], tick] + ([q['deadlineW'], q['delayW'], q['serial'], q['recvVt'], q['flags'], q['clock']] if q else []))
        return out
    first = int(re.search(r'tick=(\d+)', post[0][0]).group(1))
    last = int(re.search(r'tick=(\d+)', seg[n - 1][0]).group(1))
    a, b = window_rows(rows[:li], first, last), window_rows(rows[li:], first, last)
    part['pre_continuation_vs_post_load_window_rows'] = {'ticks': [first, last], 'rows': [len(a), len(b)], 'equal': a == b}
    part['post_load_window_rows'] = b
    part['ordercheck'] = A.ordercheck(str(obs))
    ctl = cap / 'saveload-control-1-rs-o5-saveload.txt'
    if ctl.exists():
        c = A.markers(str(ctl))
        o = A.markers(str(cap / 'saveload-observe-1-rs-o5-saveload.txt'))
        part['control'] = {'observer_preload_markers': len(o), 'control_preload_markers': len(c), 'equal': o == c,
                           'equal_without_el': strip_el(o) == strip_el(c)}
    return part


def build(root, task):
    oracle = json.loads((root / 'ORDER-05.1' / 'oracle-report-ORDER-05.1.json').read_text())
    named = [r for r in oracle['named_results'] if r['group'] == GROUP[task]]
    result = {'task': task, 'binary_sha256': oracle['binary_sha256'],
              'oracle': {k: oracle[k] for k in ('script_sha256', 'harness_sha256', 'cases', 'named', 'random', 'seed', 'mismatches', 'stats')},
              'oracle_named_results': named}
    if task in ('ORDER-05.1', 'ORDER-05.2'):
        result['live'] = {'requests': requests_part(root, task)}
    else:
        result['live'] = {'wrap': wrap_part(root), 'saveload': saveload_part(root)}
    return result


def main():
    root = Path(sys.argv[1])
    check = '--check' in sys.argv
    for task in [a for a in sys.argv[2:] if a.startswith('ORDER-')]:
        data = json.dumps(build(root, task), indent=1, sort_keys=True) + '\n'
        path = root / task / ('expected-%s.json' % task)
        if check:
            print(task, 'unchanged' if path.read_text() == data else 'DIFFERS')
            continue
        if path.exists():
            print(task, 'exists; not overwritten', sha(path))
            continue
        path.write_text(data)
        print(task, path, hashlib.sha256(data.encode()).hexdigest())


if __name__ == '__main__':
    main()
