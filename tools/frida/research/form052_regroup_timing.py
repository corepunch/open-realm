#!/usr/bin/env python3
"""FORM-05.2 regroup/rebuild timing checker (research tool, new file).

For form05/form052 observer captures, walks every physical group's owner visits (16c150 snapshots) and checks the
composed regroup rule used by the production engine (`move_group_regroup`, Payoff112 timeout fixture) against the
original's live outputs:
  status = out1 ? count - out1 - out2 : count            (16b120 result, out1 arrived, out2 near)
  status forced 0 when out1 and (cooldown or group80&4)  (reported separately; not exercised publicly here)
  advance (1697a0 from 16c565) on the same visit  <=>  status == 0  or  completion > limit(99 | 198 | 396)
  layout rebuild (16a5b0) only on the group's first visit (route admission, from 16cef4) or on an advance visit.
Writes a JSON report with the per-group timeline (visit, rel tick, status words, completion, cooldown) and violations.
"""
import argparse, hashlib, json
from pathlib import Path


def run(path):
    groups, cur, tick = {}, {}, None
    for line in Path(path).read_text().splitlines():
        if not line.strip():
            continue
        r = json.loads(line)
        ev = r.get('event')
        if ev == 'marker':
            tick = int(r['value'].split(' tick=')[1].split()[0])
        elif ev == 'tick':
            key = '%s#%d/%d' % (r['group'], r['identity'][0], r['identity'][1])
            g = groups.setdefault(key, dict(visits=0, timeline=[], layouts=[], advances=[], snapshot=None))
            g['visits'] += 1
            g['snapshot'] = r
            cur[r['group']] = key
        elif ev in ('regroup', 'advance', 'layout') and r['group'] in cur:
            g = groups[cur[r['group']]]
            if ev == 'regroup':
                s = g['snapshot']
                g['timeline'].append(dict(visit=g['visits'], tick=tick, count=s['count'], result=r['result'], out1=r['out1'], out2=r['out2'],
                                          completion=r['completion'], cooldown=r['cooldown'], gflags=s['flags'], caller=r['caller']))
            elif ev == 'advance':
                g['advances'].append(dict(visit=g['visits'], tick=tick, caller=r['caller'], reset=r['resetMembers']))
            else:
                g['layouts'].append(dict(visit=g['visits'], tick=tick, caller=r['caller']))
    report = []
    for key, g in groups.items():
        viol, checked = [], 0
        adv_visits = {a['visit'] for a in g['advances'] if a['caller'] == '16c565'}
        for t in g['timeline']:
            checked += 1
            status = t['count'] - t['out1'] - t['out2'] if t['out1'] else t['count']
            if status != t['result']:
                viol.append(['status', t])
            limit = 99 if not int(t['gflags'], 16) & 0x100 else (396 if int(t['gflags'], 16) & 0x20000 else 198)
            should = t['result'] == 0 or t['completion'] > limit
            if should != (t['visit'] in adv_visits):
                viol.append(['advance', t])
        for l in g['layouts']:
            if l['visit'] != 1 and l['visit'] not in adv_visits:
                viol.append(['layout-outside-advance', l])
        first_zero = next((t for t in g['timeline'] if t['result'] == 0), None)
        report.append(dict(group=key, visits=g['visits'], regroup_calls=checked, layouts=g['layouts'], advances=g['advances'],
                           first_status0=first_zero, max_completion=max((t['completion'] for t in g['timeline']), default=None),
                           status_sequence=[[t['visit'], t['result'], t['out1'], t['out2'], t['completion'], t['cooldown']]
                                            for i, t in enumerate(g['timeline'])
                                            if i == 0 or (t['result'], t['out1'], t['out2']) != (g['timeline'][i - 1]['result'], g['timeline'][i - 1]['out1'], g['timeline'][i - 1]['out2'])],
                           violations=viol))
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('captures', nargs='+', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    out = dict(task='FORM-05.2', sources={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in args.captures}, captures={})
    total = bad = 0
    for p in args.captures:
        rep = run(p)
        out['captures'][p.parent.parent.name + '/' + p.name] = rep
        for g in rep:
            total += g['regroup_calls']
            bad += len(g['violations'])
            print(p.name, g['group'], 'visits', g['visits'], 'regroup', g['regroup_calls'], 'layouts', [l['visit'] for l in g['layouts']],
                  'advances', [(a['visit'], a['caller']) for a in g['advances']], 'violations', len(g['violations']))
    out['regroup_calls'] = total
    out['violations'] = bad
    args.output.write_text(json.dumps(out, indent=1) + '\n')
    print('regroup calls', total, 'violations', bad, hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
