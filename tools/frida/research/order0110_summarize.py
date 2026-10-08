#!/usr/bin/env python3
"""Summarize ORDER-01.10/01.18 captures (observe JSONL and/or control Preload text).

  order0110_summarize.py --capture a.jsonl [--capture b.jsonl] [--control c-preload.txt] --output summary.json

* public: every O110/O118 marker string (exact text, rounded R2S/I2S) in emission order; repeats and the
  observer-free control must be identical.
* words: complete raw 10-word records from SaveInteger/SaveReal hooks (observe only), sha256 per capture.
* decisions: per subject unit, ordered observer events (attack/move dispatch with event code, internal task
  prepends with code/arg/caller, user admit/append/head dispatch, order factories) with pointers replaced by
  stable per-capture ordinals, so two captures can be compared exactly.
"""
import argparse, collections, hashlib, json, re, struct
from pathlib import Path

ORDER_NAMES = {851983: 'attack', 851984: 'attackground', 851985: 'attackonce', 851986: 'move', 851971: 'smart',
               851972: 'stop', 851990: 'patrol', 851991: 'patrol-two-point', 851993: 'holdposition', 0: 'none'}


def f32(word):
    return struct.unpack('<f', struct.pack('<I', word & 0xffffffff))[0]


def parse_marker(v):
    head, *rest = v.split(' ')
    d = {}
    for kv in rest:
        if '=' in kv:
            k, x = kv.split('=', 1)
            d[k] = x
    return head, d


def load_capture(path):
    events = [json.loads(l) for l in Path(path).read_text().splitlines() if l.strip()]
    markers = [e['value'] for e in events if e.get('event') == 'marker' and (e['value'].startswith('O110 ') or e['value'].startswith('O118 '))]
    rows = collections.defaultdict(dict)
    for e in events:
        if e.get('event') == 'row':
            rows[e['parent']][e['child']] = e['word']
    binds = {}
    for e in events:
        if e.get('event') == 'bind':
            binds.setdefault(e['unit'], []).append(e['handle'])
    return events, markers, rows, binds


def subject_handles(markers_rows):
    return markers_rows


def decisions(events, rows):
    # case -> subject handles from rows (word6) ; handle -> unit pointer from binds (latest binding wins per time)
    handle_case = {}
    for r, w in rows.items():
        if 6 in w and 1 in w and w.get(6):
            handle_case.setdefault(w[6], w[1])
    unit_case = {}
    ordinals = {}

    def ordinal(p):
        if p is None:
            return None
        if p not in ordinals:
            ordinals[p] = len(ordinals)
        return ordinals[p]

    out = collections.defaultdict(list)
    tick = 0
    keep = ('attack-dispatch-begin', 'move-dispatch-begin', 'task-prepend', 'user-admit', 'user-append',
            'user-head-dispatch-begin', 'order-factory-immediate', 'order-factory-point')
    for e in events:
        ev = e.get('event')
        if ev == 'marker' and e['value'].startswith('PATHMETA complete'):
            break  # later events depend on wall-clock capture shutdown, not on the scenario
        if ev == 'marker':
            m = re.match(r'PATHTRACE tick=(\d+) ', e['value'])
            if m:
                tick = int(m.group(1)) + 1
        elif ev == 'bind':
            if e['handle'] in handle_case:
                unit_case[e['unit']] = handle_case[e['handle']]
        elif ev in keep:
            u = e.get('unit')
            case = unit_case.get(u)
            if case is None and ev.startswith('order-factory'):
                case = 'factory'
            if case is None:
                continue
            row = {'tick': tick, 'event': ev, 'unit': ordinal(u)}
            if 'code' in e:
                row['code'] = '%x' % e['code']
            if e.get('input'):
                i = e['input']
                row['input'] = {'command': i['command'], 'point': i['point'], 'alternate': i['alternate'], 'flags20': '%x' % i['flags20']}
            if isinstance(e.get('task'), dict):
                row['task'] = {'code': '%x' % e['task']['code']}
            if ev == 'task-prepend':
                # Event tasks (factory 691f20, return 691fd3) leave +34 uninitialized heap data: not compared.
                row.update(code='%x' % e['code'], caller='%x' % (0x6f000000 + e['caller']))
                if e['caller'] != 0x691fd3:
                    row['arg'] = '%x' % e['arg']
            if ev in ('user-admit', 'user-append', 'user-head-dispatch-begin'):
                o = e.get('order') or {}
                row.update(command=o.get('command'), point=o.get('point'), alternate=o.get('alternate'),
                           caller='%x' % (0x6f000000 + e['caller']), count_before=e['before']['count'])
                if ev == 'user-admit':
                    row.update(mode=e['mode'], dispatch=e['dispatch'], count_after=e['after']['count'], result=e['result'])
                if ev == 'user-append':
                    row.update(count_after=e['after']['count'])
            if ev.startswith('order-factory'):
                row.update(command=e['command'], caller='%x' % (0x6f000000 + e['caller']))
                if 'point' in e:
                    row.update(point=e['point'], alternate=e['alternate'])
            if ev in ('attack-dispatch-begin', 'move-dispatch-begin'):
                row.update(caller='%x' % (0x6f000000 + e['caller']), count=e['before']['count'], flags='%x' % e['abilityFlags'])
            out[str(case)].append(row)
    return out


def timelines(markers):
    per = collections.defaultdict(list)
    for v in markers:
        head, d = parse_marker(v)
        if 'row' not in d:
            continue
        per[d['case']].append(d)
    result = {}
    for case, rows in sorted(per.items(), key=lambda x: int(x[0])):
        changes, damage, last = [], [], None
        for d in rows:
            order = int(d['order'])
            if d['label'] == 'damage':
                damage.append({'tick': int(d['tick']), 'source_order': order, 'amount': d['w3'], 'source': int(d['src'])})
                continue
            if d['label'] != 'sample' or order != last:
                changes.append({'tick': int(d['tick']), 'label': d['label'], 'order': order, 'name': ORDER_NAMES.get(order, str(order)),
                                'x': d['x'], 'y': d['y'], 'accepted': int(d['acc']), 'enemy_life': d['w3']})
            last = order
        result[case] = {'transitions': changes, 'damage': damage}
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--capture', action='append', default=[])
    ap.add_argument('--control', action='append', default=[])
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    report = {'captures': [], 'controls': []}
    public_sets = []
    for c in args.capture:
        events, markers, rows, binds = load_capture(c)
        words = [[rows[r].get(k) for k in range(10)] for r in sorted(rows)]
        dec = decisions(events, rows)
        meta = next((e for e in events if e.get('event') == 'metadata'), {})
        failed = [e for e in events if e.get('event') == 'trace-failed' or e.get('type') == 'error']
        pre = next((e for e in events if e.get('event') == 'preload-file'), {})
        entry = {'path': c, 'sha256': hashlib.sha256(Path(c).read_bytes()).hexdigest(), 'map_sha256': meta.get('source_sha256', {}).get('map'),
                 'records': len(words), 'markers': len(markers), 'failed': bool(failed), 'preload_complete': pre.get('complete'),
                 'public_sha256': hashlib.sha256('\n'.join(markers).encode()).hexdigest(),
                 'words_sha256': hashlib.sha256(json.dumps(words).encode()).hexdigest(),
                 'decisions_sha256': hashlib.sha256(json.dumps(dec, sort_keys=True).encode()).hexdigest(),
                 'observer_events': collections.Counter(e.get('event') for e in events)}
        report['captures'].append(entry)
        public_sets.append(markers)
        if len(report['captures']) == 1:
            report['timelines'] = timelines(markers)
            report['decisions'] = dec
            report['words_first'] = words
    for c in args.control:
        raw = Path(c).read_bytes()
        markers = re.findall(r'call Preload\( "((?:O110|O118) [^"\r\n]*)" \)', raw.decode('utf-8', 'replace'))
        report['controls'].append({'path': c, 'sha256': hashlib.sha256(raw).hexdigest(), 'markers': len(markers),
                                   'public_sha256': hashlib.sha256('\n'.join(markers).encode()).hexdigest()})
        public_sets.append(markers)
        if 'timelines' not in report:
            report['timelines'] = timelines(markers)
    report['public_identical'] = len({hashlib.sha256('\n'.join(m).encode()).hexdigest() for m in public_sets}) == 1 if public_sets else None
    if len(public_sets) > 1 and not report['public_identical']:
        first = public_sets[0]
        diffs = []
        for k, other in enumerate(public_sets[1:], 1):
            for i, (a, b) in enumerate(zip(first, other)):
                if a != b:
                    diffs.append({'set': k, 'index': i, 'first': a, 'other': b})
                    break
            else:
                if len(first) != len(other):
                    diffs.append({'set': k, 'length': [len(first), len(other)]})
        report['public_first_differences'] = diffs
    dsets = [c['decisions_sha256'] for c in report['captures']]
    report['decisions_identical'] = len(set(dsets)) == 1 if dsets else None
    report['words_identical'] = len({c['words_sha256'] for c in report['captures']}) == 1 if report['captures'] else None
    args.output.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({k: report[k] for k in ('public_identical', 'decisions_identical', 'words_identical')}),
          [(c['records'], c['markers'], c['failed']) for c in report['captures']], [(c['markers']) for c in report['controls']])


if __name__ == '__main__':
    main()
