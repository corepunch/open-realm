#!/usr/bin/env python3
"""TARGET-02.1: freeze compact expected per-owner-tick pursuit decisions from target021_analyze.py reports.

Row layout per follower owner visit (all raw uint32 words unless noted; -1 = absent):
 [c, group_index, cd_before, cd_after, unseen_before, unseen_after, vis_blocked, sample_x, sample_y,
  g_changed, g_ready, g_t7c, g_t80, req_result, req_ready, req_final,
  m_changed, m_ready, m_t7c, m_t80, m_old_x, m_old_y, m_new_x, m_new_y,
  g_coarse_pops, m_coarse_pops, m_fine_pops, m_fine_bucket_work, g_setdest, m_setdest, refresh_reload,
  self_pos_x, self_pos_y]
Two observed repeats must agree exactly on these rows (pointer values excluded) before freezing.
"""
import argparse
import hashlib
import json
from pathlib import Path

FIELDS = ['c', 'gi', 'cd_before', 'cd_after', 'unseen_before', 'unseen_after', 'vis_blocked', 'sample_x', 'sample_y',
          'g_changed', 'g_ready', 'g_t7c', 'g_t80', 'req_result', 'req_ready', 'req_final',
          'm_changed', 'm_ready', 'm_t7c', 'm_t80', 'm_old_x', 'm_old_y', 'm_new_x', 'm_new_y',
          'g_coarse_pops', 'm_coarse_pops', 'm_fine_pops', 'm_fine_bucket_work', 'g_setdest', 'm_setdest', 'refresh_reload',
          'self_pos_x', 'self_pos_y']


def f2w(x):
    import struct
    return struct.unpack('<I', struct.pack('<f', x))[0]


def rows_for(scene):
    out = []
    for t in scene['timeline']:
        d = t['decisions']
        r = dict.fromkeys(FIELDS, -1)
        r['c'] = t['c']
        r['gi'] = t['gi']
        r['cd_before'] = t['cd']
        r['unseen_before'] = t['unseen']
        if t.get('after'):
            r['cd_after'] = t['after']['cd']
            r['unseen_after'] = t['after']['unseen']
        for e in d:
            k = e['ev']
            if k == 'vis' and r['vis_blocked'] == -1:
                r['vis_blocked'] = e['blocked']
            elif k == 'sample' and r['sample_x'] == -1:
                r['sample_x'], r['sample_y'] = e['dest_words']
            elif k == 'gate' and e['path'] == 'group' and r['g_changed'] == -1:
                r['g_changed'], r['g_ready'] = e['changed'], e['ready']
                r['g_t7c'], r['g_t80'] = e['times']
            elif k == 'gate' and e['path'] == 'self' and r['m_changed'] == -1:
                r['m_changed'], r['m_ready'] = e['changed'], e['ready']
                r['m_t7c'], r['m_t80'] = e['times']
                r['m_old_x'], r['m_old_y'] = e['old_words']
                r['m_new_x'], r['m_new_y'] = e['new_words']
            elif k == 'req' and r['req_result'] == -1:
                r['req_result'], r['req_ready'], r['req_final'] = e['result'], e['ready'], e['final']
            elif k == 'coarse' and e['path'] == 'group' and e['search']:
                r['g_coarse_pops'] = e['search']['pops']
            elif k == 'coarse' and e['path'] == 'self' and e['search']:
                r['m_coarse_pops'] = e['search']['pops']
            elif k == 'fine' and e['path'] == 'self' and e['search']:
                r['m_fine_pops'] = e['search']['pops']
                r['m_fine_bucket_work'] = e['search']['work']
            elif k == 'setdest' and e['path'] == 'group':
                r['g_setdest'] = 1
            elif k == 'setdest' and e['path'] == 'self':
                r['m_setdest'] = 1
            elif k == 'refresh':
                r['refresh_reload'] = e['reload']
        if t.get('self'):
            r['self_pos_x'], r['self_pos_y'] = (t['self']['pos_words'] if 'pos_words' in t['self']
                                                  else [f2w(v) for v in t['self']['pos']])
        out.append([r[f] for f in FIELDS])
    return out


def lite_rows(scene):
    """Compact ordered decisions for multi-follower (lite) scenes: one row per owner visit with decisions."""
    out = []
    for t in scene['timeline']:
        ev = []
        for e in t['decisions']:
            k = e['ev']
            if k == 'sample':
                ev.append(['s', e['cd'][0], e['cd'][1], e['unseen'][0], e['unseen'][1]] + e['dest_words'])
            elif k == 'vis':
                ev.append(['v', e['blocked']])
            elif k == 'gate':
                ev.append(['g' if e['path'] == 'group' else 'm', e['changed'], e['ready']] + e['times'] + e['old_words'] + e['new_words'])
            elif k == 'req':
                ev.append(['q', e['result'], e['ready']])
            elif k == 'interval':
                ev.append(['i', 0 if e['path'] == 'group' else 1, e['mode'], e['result'], e['elapsed']] + e['ts'])
            elif k == 'admit':
                ev.append(['a', 0 if e['path'] == 'group' else 1, e['result'], e['work'], e['limit']] + e['qcount'])
            elif k in ('coarse', 'fine'):
                ev.append([k[0], 0 if e['path'] == 'group' else 1, e['result'], e['search']['pops'] if e['search'] else -1,
                           e['search']['work'] if e['search'] else -1] + e['times'][1])
            elif k == 'setdest':
                ev.append(['d', 0 if e['path'] == 'group' else 1] + e['dest_words'])
            elif k == 'refresh':
                ev.append(['r', e['reload'], e['unclamped'] if e['unclamped'] is not None else -1])
        if ev:
            out.append([t['c'], t['gi'], t['cd'], t['unseen'], ev])
    return out


def scene_summary(s):
    import struct
    f = lambda w: struct.unpack('<f', struct.pack('<I', w))[0] if w is not None else None
    tasks = [dict(c=t['c'], event=t['event'], caller=hex(t['caller']), range_word=t.get('range'), range=f(t.get('range')),
                  persistent=t.get('persistent'), result=t.get('result')) for t in s['tasks']
             if t['event'] in ('begin-task', 'begin-target', 'validate')]
    return dict(scene=s['scene'], window=s['window'], counts=s['counts'], tasks=tasks,
                followers=s['followers'], targets=s['targets'],
                groups=[dict(flags=g['flags_first'], visits=g['visits'], c=g['c'], target=g['target'], members=g['members']) for g in s['groups']],
                delayed=[{k: v for k, v in d.items()} for d in s['delayed']],
                markers=[m for m in s['markers']], samples=[[m['local'], m['rest']] for m in s['samples']],
                violations=len(s['violations']))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('reports', type=Path, nargs='+', help='analyzer reports of observed repeats (first is frozen)')
    ap.add_argument('--names', nargs='+', required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--extra', type=Path, help='json merged under "context"')
    a = ap.parse_args()
    reps = [json.loads(p.read_text()) for p in a.reports]
    base = reps[0]
    cmp = []
    for i, r in enumerate(reps[1:], 1):
        same = []
        for s0, s1 in zip(base['scenes'], r['scenes']):
            lite = len(s0['followers']) > 2
            r0, r1 = (lite_rows(s0), lite_rows(s1)) if lite else (rows_for(s0), rows_for(s1))
            c0 = r0[0][0] if r0 else 0
            c1 = r1[0][0] if r1 else 0
            rel0 = [[x[0] - c0] + x[1:] for x in r0]
            rel1 = [[x[0] - c1] + x[1:] for x in r1]
            first = next((k for k, (x, y) in enumerate(zip(rel0, rel1)) if x != y), None)
            same.append(dict(scene=s0['scene'], rows=[len(r0), len(r1)], identical_relative=rel0 == rel1, first_difference=first,
                             counter_offset=c1 - c0, samples_identical=[m['rest'] for m in s0['samples']] == [m['rest'] for m in s1['samples']]))
        cmp.append(dict(report=a.names[i], scenes=same))
    out = dict(task='TARGET-02.1', fields=FIELDS, source_reports=[dict(name=n, capture_sha256=r['capture_sha256']) for n, r in zip(a.names, reps)],
               repeat_comparison=cmp,
               scenes=[dict(**scene_summary(s), **({'lite_rows': lite_rows(s)} if len(s['followers']) > 2 else {'rows': rows_for(s)}))
                       for s in base['scenes']])
    if a.extra:
        out['context'] = json.loads(a.extra.read_text())
    a.output.write_text(json.dumps(out, separators=(',', ':')) + '\n')
    print(hashlib.sha256(a.output.read_bytes()).hexdigest(), a.output.stat().st_size)
    print(json.dumps(cmp, indent=1)[:4000])


if __name__ == '__main__':
    main()
