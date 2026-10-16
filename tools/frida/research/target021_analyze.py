#!/usr/bin/env python3
"""TARGET-02.1 analyzer: per-scene, per-owner-tick pursuit decisions from a target021_observer capture.

For every scene window (begin-setup .. end-cleanup markers) it identifies the follower mover from the
public order's target request (05a5c0 self identity), follows every physical group that contains that
mover, and emits the complete ordered owner-visit timeline: refresh countdown, visibility callback,
sampled destination, group gate, route request, member gates, request-interval/admission decisions,
coarse/fine searches with pops and timestamp writes, destination writes and countdown reloads.
It also lists every delayed changed-cell gate (changed=1, ready=0), the search that produced the
blocking timestamp and the first later acceptance, and checks the static rules recovered in the handoff.
"""
import argparse
import collections
import hashlib
import json
import math
import re
import struct
from pathlib import Path

GROUP_GATE = 0x16ce1f  # return address of 167e40 inside 16ce10
MEMBER_GATE = 0x16fc6a  # return address of 167e40 inside 16fbd0 (checked against the capture)
NO_ID = [0xffffffff, 0xffffffff]


def f32(w):
    return struct.unpack('<f', struct.pack('<I', w & 0xffffffff))[0]


def fv(ws):
    return [round(f32(w), 6) for w in ws]


def bucket(ws, shift=1):
    return [math.floor(f32(w)) >> shift for w in ws]


def load(path):
    rows = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def parse_marker(v):
    m = re.match(r'(\S+) tick=(\d+) s=(-?\d+) l=(\d+) label=(\S+)(.*)', v)
    if not m:
        m2 = re.match(r'(\S+) tick=(\d+) .*label=(\S+)', v)
        return dict(tick=int(m2.group(2)), scene=None, local=None, label=m2.group(3), rest='') if m2 else None
    return dict(tick=int(m.group(2)), scene=int(m.group(3)), local=int(m.group(4)), label=m.group(5), rest=m.group(6).strip())


def segment(rows):
    """Split the stream into group visits (gtick .. gtick-end of the same group) and outside events."""
    visits, outside, cur = [], [], None
    for i, r in enumerate(rows):
        e = r.get('event')
        if e == 'gtick':
            if cur is not None:
                cur['unterminated'] = True
                visits.append(cur)
            cur = dict(start=r, events=[], end=None, index=i)
        elif e == 'gtick-end' and cur is not None and r['g'] == cur['start']['g']:
            cur['end'] = r
            visits.append(cur)
            cur = None
        elif cur is not None:
            cur['events'].append(r)
        else:
            outside.append(r)
    if cur is not None:
        cur['unterminated'] = True
        visits.append(cur)
    return visits, outside


def compact_event(e, paths):
    k = e['event']
    role = lambda p: paths.get(p, p)
    if k == 'vis':
        return dict(ev='vis', blocked=e['blocked'])
    if k == 'sample':
        return dict(ev='sample', cd=[e['before']['cd'], e['cd']], unseen=[e['before']['unseen'], e['unseen']], dest=fv(e['dest']), dest_words=e['dest'])
    if k == 'gate':
        return dict(ev='gate', path=role(e['p']), caller=hex(e['caller']), old=fv(e['old']), new=fv(e['nw']), old_words=e['old'], new_words=e['nw'],
                    times=e['times'], changed=e['changed'], ready=e['ready'])
    if k == 'req':
        return dict(ev='req', result=e['result'], ready=e['ready'], final=e['final'], dest=fv(e['path']['dest']) if e['path'] else None,
                    times=e['path']['times'] if e['path'] else None, idx=e['path']['idx'] if e['path'] else None, cnt=e['path']['cnt'] if e['path'] else None)
    if k == 'interval':
        return dict(ev='interval', path=role(e['p']), mode=e['mode'], cur=e['cur'], ts=[e['before'], e['after']], elapsed=e['elapsed'], result=e['result'], caller=hex(e['caller']))
    if k == 'admit':
        return dict(ev='admit', path=role(e['p']), bucket=e['bucket'], work=e['work'], limit=e['limit'], qcount=[e['qcount'], e['qcountAfter']], queued=e['queued'], result=e['result'], caller=hex(e['caller']))
    if k in ('coarse', 'fine'):
        return dict(ev=k, path=role(e['p']), result=e['result'], times=[e['times'], e['timesAfter']], search=e['search'], cnt=e['cnt'], idx=e['idx'], caller=hex(e['caller']))
    if k == 'setdest':
        return dict(ev='setdest', path=role(e['p']), dest=fv(e['dest']), dest_words=e['dest'], replace=e['replace'], caller=hex(e['caller']))
    if k == 'refresh':
        return dict(ev='refresh', reload=e['reload'], unclamped=e.get('unclamped'), dist=f32(e['dist']) if 'dist' in e else None)
    if k == 'activate':
        return dict(ev='activate', path=role(e['p']), times=e['times'], caller=hex(e['caller']))
    return dict(ev=k, **{('event_ptr' if x == 'ev' else x): y for x, y in e.items() if x not in ('event', 'ms')})


def analyze(rows):
    meta = next(r for r in rows if r.get('event') == 'metadata')
    markers = [dict(**parse_marker(r['value']), c=r['c'], value=r['value']) for r in rows if r.get('event') == 'marker' and parse_marker(r['value'])]
    visits, outside = segment(rows)
    windows = []
    for m in markers:
        if m['label'] == 'begin-setup':
            windows.append(dict(scene=m['scene'], begin=m['c'], end=None, markers=[]))
        if windows and windows[-1]['end'] is None:
            windows[-1]['markers'].append(m)
        if m['label'] == 'end-cleanup' and windows and windows[-1]['end'] is None:
            windows[-1]['end'] = m['c']
    scenes = []
    for w in windows:
        lo, hi = w['begin'], (w['end'] if w['end'] is not None else 1 << 62)
        inside = lambda r: r.get('c') is not None and lo <= r['c'] <= hi
        tasks = [r for r in rows if r.get('event') in ('begin-task', 'begin-target', 'validate', 'target-lost', 'on-target-lost', 'set-target', 'vis-query') and inside(r)]
        selves = []
        for r in tasks:
            if r['event'] == 'begin-target' and r['self'] not in selves:
                selves.append(r['self'])
        follower = selves[0] if selves else None
        targets = sorted({tuple(r['target']) for r in tasks if r['event'] == 'begin-target'})
        fvisits = [v for v in visits if inside(v['start']) and
                   (any(m.get('id') in selves for m in v['start'].get('members', [])) or v['start'].get('target') not in (None, NO_ID))]
        groups = collections.OrderedDict()
        for v in fvisits:
            g = v['start']['g'] + '@' + ','.join(map(str, v['start']['id']))
            groups.setdefault(g, []).append(v)
        timeline, delayed, gate_rows = [], [], []
        group_summ = []
        for gkey, vs in groups.items():
            first = vs[0]['start']
            gpaths = {first['path']['p']: 'group'} if first.get('path') else {}
            for v in vs:
                for mm in v['start']['members']:
                    if mm.get('path'):
                        gpaths[mm['path']['p']] = 'self' if mm.get('id') == follower else 'follower' if mm.get('id') in selves else 'member'
            group_summ.append(dict(group=gkey, target=first['target'], flags_first=hex(first['flags']), visits=len(vs),
                                   c=[vs[0]['start']['c'], vs[-1]['start']['c']], members=[m.get('id') for m in first['members']]))
            gi = len(group_summ) - 1
            for v in vs:
                s = v['start']
                me = next((m for m in s['members'] if m.get('id') == follower), None)
                row = dict(c=s['c'], gi=gi, group=gkey, target=s['target'], flags=hex(s['flags']), cd=s['cd'], unseen=s['unseen'],
                           gpath=dict(dest=fv(s['path']['dest']), times=s['path']['times'], idx=s['path']['idx'], cnt=s['path']['cnt']) if s.get('path') else None,
                           self=dict(pos=fv(me['pos']), range=f32(me['range']) if 'range' in me else None, flags=hex(me['flags']) if 'flags' in me else None,
                                     dest=fv(me['path']['dest']), times=me['path']['times'],
                                     idx=me['path']['idx'], cnt=me['path']['cnt'], retry=me['path'].get('retry')) if me and me.get('path') else None,
                           decisions=[compact_event(e, gpaths) for e in v['events']],
                           after=dict(cd=v['end']['cd'], unseen=v['end']['unseen'], flags=hex(v['end']['flags'])) if v.get('end') else None)
                timeline.append(row)
                for e in v['events']:
                    if e['event'] == 'gate':
                        gr = dict(c=e['c'], group=gkey, path=gpaths.get(e['p'], e['p']), p=e['p'], caller=e['caller'], old=e['old'], new=e['nw'],
                                  times=e['times'], changed=e['changed'], ready=e['ready'])
                        gate_rows.append(gr)
        # delayed gates and their producers
        intervals = [r for v in visits for r in v['events'] if r['event'] == 'interval'] + [r for r in outside if r.get('event') == 'interval']
        searches = [r for v in visits for r in v['events'] if r['event'] in ('coarse', 'fine')] + [r for r in outside if r.get('event') in ('coarse', 'fine')]
        for i, g in enumerate(gate_rows):
            if g['changed'] and not g['ready']:
                producers = []
                for mode, ts in enumerate(g['times']):
                    if ts and ((g['c'] - ts) & 0xffffffff) < 10:
                        iv = [r for r in intervals if r['p'] == g['p'] and r['mode'] == mode and r['after'] == ts and r['result'] == 1]
                        se = [r for r in searches if r['p'] == g['p'] and r['c'] == ts and r['event'] == ('fine' if mode == 0 else 'coarse')]
                        producers.append(dict(mode=('fine' if mode == 0 else 'coarse'), timestamp=ts, age=(g['c'] - ts) & 0xffffffff,
                                              interval_pass=[dict(c=r['c'], caller=hex(r['caller']), elapsed=r['elapsed']) for r in iv],
                                              searches=[dict(c=r['c'], result=r['result'], times_after=r['timesAfter'], search=r['search'], caller=hex(r['caller'])) for r in se]))
                later = [h for h in gate_rows[i + 1:] if h['p'] == g['p']]
                nxt = next((h for h in later if h['ready'] and h['changed']), None)
                same = next((h for h in later if not h['changed']), None)
                delayed.append(dict(c=g['c'], path=g['path'], caller=hex(g['caller']), old=fv(g['old']), new=fv(g['new']),
                                    old_bucket=bucket(g['old']), new_bucket=bucket(g['new']), times=g['times'], producers=producers,
                                    next_accept=dict(c=nxt['c'], new=fv(nxt['new']), times=nxt['times']) if nxt else None,
                                    next_unchanged=dict(c=same['c'], new=fv(same['new'])) if same else None))
        # rule checks
        violations = []
        for g in gate_rows:
            ch = int(bucket(g['old']) != bucket(g['new']))
            rd = int(not ch or all(((g['c'] - t) & 0xffffffff) >= 10 for t in g['times']))
            if (ch, rd) != (g['changed'], g['ready']):
                violations.append(dict(rule='167e40', gate=g))
        for row in timeline:
            dec = row['decisions']
            for d in dec:
                if d['ev'] == 'refresh' and d['unclamped'] is not None:
                    exp = max(16, min(132, d['unclamped'])) + (165 if int(row['flags'], 16) & 0x400 else 0)
                    if d['reload'] != exp:
                        violations.append(dict(rule='169680 clamp', c=row['c'], refresh=d))
            samp = [d for d in dec if d['ev'] == 'sample']
            vis = [d for d in dec if d['ev'] == 'vis']
            if samp and row['target'] != NO_ID:
                s = samp[0]
                blocked = vis[0]['blocked'] if vis else 0
                if blocked:
                    if s['unseen'][1] != s['unseen'][0] + 1:
                        violations.append(dict(rule='16cd30 unseen increment', c=row['c']))
                elif s['cd'][0] == 0 and s['cd'][1] != -1:
                    violations.append(dict(rule='16cd30 sample marks -1', c=row['c']))
                elif s['cd'][0] != 0 and s['cd'][1] != s['cd'][0]:
                    violations.append(dict(rule='16cd30 retains countdown', c=row['c']))
            for d in dec:
                if d['ev'] == 'interval':
                    exp = int(((d['cur'] - (d['ts'][0] if d['ts'][0] <= d['cur'] else d['cur'] - 10)) & 0xffffffff) >= 10)
                    if exp != d['result']:
                        violations.append(dict(rule='168910', c=row['c'], interval=d))
                if d['ev'] in ('coarse', 'fine') and d['search']:
                    keep = d['search']['pops'] >= 32 if d['ev'] == 'coarse' else d['search']['work'] >= 64
                    mode = 1 if d['ev'] == 'coarse' else 0
                    if d['result'] and (d['times'][1][mode] == 0) == keep:
                        violations.append(dict(rule='timestamp retention', c=row['c'], search=d))
        mk = [dict(tick=m['tick'], local=m['local'], c=m['c'], label=m['label'], rest=m['rest']) for m in w['markers'] if m['label'] != 'sample']
        samples = [dict(tick=m['tick'], local=m['local'], c=m['c'], rest=m['rest']) for m in w['markers'] if m['label'] == 'sample']
        scenes.append(dict(scene=w['scene'], window=[w['begin'], w['end']], follower=follower, followers=selves, targets=[list(t) for t in targets],
                           tasks=[{k: v for k, v in r.items() if k != 'ms'} for r in tasks], groups=group_summ,
                           counts=dict(visits=len(fvisits), gates=len(gate_rows), group_gates=sum(1 for g in gate_rows if g['path'] == 'group'),
                                       member_gates=sum(1 for g in gate_rows if g['path'] == 'self'),
                                       changed=sum(1 for g in gate_rows if g['changed']), delayed=len(delayed),
                                       delayed_group=sum(1 for d in delayed if d['path'] == 'group'), delayed_self=sum(1 for d in delayed if d['path'] == 'self'),
                                       delayed_follower=sum(1 for d in delayed if d['path'] == 'follower'), followers=len(selves),
                                       admit_denied=sum(1 for r in timeline for d in r['decisions'] if d['ev'] == 'admit' and not d['result']),
                                       refresh=sum(1 for r in timeline for d in r['decisions'] if d['ev'] == 'refresh')),
                           delayed=delayed, violations=violations, markers=mk, samples=samples, timeline=timeline))
    return dict(capture_meta={k: meta.get(k) for k in ('task', 'mode', 'map', 'source_sha256', 'display', 'remote')}, scenes=scenes)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', type=Path)
    ap.add_argument('--report', type=Path, required=True)
    ap.add_argument('--summary', action='store_true')
    a = ap.parse_args()
    rows = load(a.capture)
    rep = analyze(rows)
    rep['capture_sha256'] = hashlib.sha256(a.capture.read_bytes()).hexdigest()
    a.report.write_text(json.dumps(rep, separators=(',', ':')))
    for s in rep['scenes']:
        print(s['scene'], s['window'], 'follower', s['follower'], 'groups', len(s['groups']), s['counts'], 'violations', len(s['violations']))


if __name__ == '__main__':
    main()
