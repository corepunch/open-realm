#!/usr/bin/env python3
"""Check real UI formation-option producers and their first complete owner tick.

Only the first tick is compared between independent input captures. External
input lands at different absolute clocks; later collision/arrival histories
are retained but are not asserted to repeat.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from verify_wc3_scheduler_trace import HASH

SOURCE_HASHES = {
    'wc3_formation_policy_probe.j': 'fa3edcff83de127dae2939d9d9e176c8469ca61bd070521548a65e2d273450e1',
    'trace_wc3_pathfinding.py': '5b9b5a9157848b99c8b284264d5369917f4c75ad6d7d79085ac079dfa0cede08',
    'wc3_pathfinding.js': '2aafc288bd5086b1fa73c3203de0f8812d8c9f666e5df33c8ff72f7a20419f5a',
    'make_wc3_pathfinding_map.py': 'dbb7b1dfce4979b676980a177f7f554ecfe1a68d97a811436b8a150830925613',
    'wc3_ui_input.c': '7a457bb0cd14c8b633a5f1c06ccf8b7e0a125e7503daf1323bfce2b9c548cfb4',
    'wc3_ui_input.exe': '675c94df1da19308c32bbcfb3f21ea1d6c35abe71d6515b1876d725b60e12f83',
    'wc3_ui_input.exe.so': '0b66ec6a9c3206dcaa427221b432173e6d45247dfeb4d1ff235155db75bd8bd8',
    'map': 'aae5ae09dd5a7c8175aaa86559f09419e7bebfcebc510fc88cfed694482f3969',
}
EVENTS = ('metadata','trace-end','marker','player-input-helper','player-move-click',
          'player-point-action-begin','player-point-action-end','formation-policy-set',
          'formation-authored-rank','formation-rank-set','formation-rank-layout',
          'formation-rank-buckets','group-routing-radius','pair-group-phase-begin',
          'pair-group-phase-end','motion-decision','velocity-commit')
COUNTED = ('formation-policy-set','formation-authored-rank','formation-rank-set',
           'formation-rank-layout','formation-rank-buckets','motion-decision','velocity-commit')


def first_tick(rows):
    action = next(r for r in rows if r['event']=='player-point-action-begin')
    layout = next(r for r in rows if r['event']=='formation-rank-layout')
    begin = next(r for r in rows if r['event']=='pair-group-phase-begin' and r['phase']=='decide')
    end = next(r for r in rows if r['event']=='pair-group-phase-end' and r['phase']=='decide')
    commit = next(r for r in rows if r['event']=='pair-group-phase-end' and r['phase']=='commit')
    commits = [r for r in rows if r['event']=='velocity-commit'][:6]
    decisions = [r for r in rows if r['event']=='motion-decision'][:6]
    if any(r['identity']!=begin['identity'] or r['group']!=begin['group'] for r in (layout['after'],end,commit)):
        raise ValueError('first tick changed owner identity')
    if any(len(r['members'])!=6 for r in (layout['after'],begin,end,commit)) or len(commits)!=6:
        raise ValueError('first tick lost a member')
    if len({r['mover'] for r in commits})!=6:
        raise ValueError('first tick repeated a commit')
    tick=[]
    for a,b,c,d,e in zip(layout['after']['members'],begin['members'],end['members'],commit['members'],commits):
        if not a['mover']==b['mover']==c['mover']==d['mover']==e['mover']:
            raise ValueError('first tick reordered member observations')
        if a['row'][3:5]!=b['row'][3:5] or b['row'][3:5]!=c['row'][3:5] or b['pose']!=e['before']:
            raise ValueError('layout/prediction observation differs')
        if c['row']!=d['row'] or c['row'][8:10]!=e['requested'] or e['after']!=d['pose']:
            raise ValueError('decision/commit observation differs')
        tick.append([*b['row'][3:5],*c['row'][6:11],*e['after'],*e['requested'],e['speed'],e['heading']])
    if len(decisions)!=6 or any(r['mover']!=c['mover'] or [r['nextSpeed'],r['nextHeading']]!=c['requested'] for r,c in zip(decisions,commits)):
        raise ValueError('decision output did not reach the ordered commit')
    policy=14 if action['flags']&16 else 0
    if (begin['flags']&14)!=policy or (end['flags']&14)!=policy or (commit['flags']&14)!=policy:
        raise ValueError('canonical policy lost at physical owner')
    if [r[6] for r in tick] != ([0x100000]*6 if policy else [0x100000,0x200000,0x200000,0x200000,0x100000,0x200000]):
        raise ValueError('mixed-rank hold classification differs')
    return {'input':[action['clock'][0],action['counter'],*action['point'],*commits[0]['clock']],
            'tick':tick,'flags':begin['flags'],'formation':begin['formation'],
            'geometry':[ *begin['formation'],layout['heading'],next(r for r in rows if r['event']=='group-routing-radius')['result']]}


def signature(result):
    # Keep every scalar except the absolute commit time. Input clocks are
    # separately supplied to each engine replay, never rounded/subtracted.
    return {'point':result['input'][2:4], 'flags':result['flags'],
            'formation':result['formation'],
            'tick':[r[:7]+r[8:] for r in result['tick']]}


def verify(rows):
    metadata=[r for r in rows if r.get('event')=='metadata']
    footer=[r for r in rows if r.get('event')=='trace-end']
    if len(metadata)!=1 or metadata[0].get('sha256')!=HASH:
        raise ValueError('missing pinned policy metadata')
    meta=metadata[0]
    if any(meta.get('source_sha256',{}).get(k)!=v for k,v in SOURCE_HASHES.items()):
        raise ValueError('policy producer/observer generation differs')
    if not all(meta.get(k) for k in ('profileEvents','taskEvents','motionEvents','velocityEvents')):
        raise ValueError('policy stage observer disabled')
    if len(footer)!=1 or footer[0]!=rows[-1] or not footer[0].get('installed'):
        raise ValueError('incomplete policy capture')
    if any(r.get('event') in ('error','trace-failed') for r in rows):
        raise ValueError('observer failed')
    counts=Counter(r.get('event') for r in rows)
    for event in COUNTED:
        if counts[event]!=footer[0]['counts'].get(event):
            raise ValueError('truncated '+event)
    markers=[r['value'] for r in rows if r.get('event')=='marker']
    if [int(re.search(r'tick=(\d+)',v)[1]) for v in markers if 'label=sample ' in v]!=list(range(1,121)):
        raise ValueError('policy producer samples missing')
    if [v.split('label=')[1].split()[0] for v in markers if 'label=sample ' not in v]!=[
            'start_formation_policy','created','complete']:
        raise ValueError('policy producer boundaries missing')
    config=meta.get('pointInput',{})
    if config.get('api')!='external Win32 SendInput' or not config.get('nativeKey') or not config.get('sampleTicks'):
        raise ValueError('input did not use reviewed owned native helper')
    actions=[r for r in rows if r['event']=='player-point-action-begin']
    ends=[r for r in rows if r['event']=='player-point-action-end']
    clicks=[r for r in rows if r['event']=='player-move-click']
    helpers=[r for r in rows if r['event']=='player-input-helper']
    expected=1+len(config.get('extra',[]))
    if not len(actions)==len(ends)==len(clicks)==len(helpers)==expected:
        raise ValueError('native input/admission missing')
    for a,b,c,h in zip(actions,ends,clicks,helpers):
        if {k:v for k,v in a.items() if k not in ('event','ms')}!={k:v for k,v in b.items() if k not in ('event','ms')} or a['entry']!=0x6b9f70 or a['order']!=851986 or a['player']!=3:
            raise ValueError('point producer differs')
        flags=8+(16 if config.get('alt') else 0)+(1 if config.get('shift') else 0)
        if a['flags']!=flags or c['alt']!=bool(config.get('alt')) or c['shift']!=bool(config.get('shift')):
            raise ValueError('input modifier differs')
        if h['sha256']!=SOURCE_HASHES['wc3_ui_input.exe'] or 'down/up accepted' not in h['output']:
            raise ValueError('native helper did not accept input')
    policy=[r for r in rows if r['event']=='formation-policy-set']
    requests={}
    for r in policy:
        key=tuple(r['identity']);requests.setdefault(key,[]).append(r)
        enabled=int(bool(config.get('alt')))
        mask=r['mask']
        if mask not in (2,4,8) or r['enabled']!=enabled or r['after']!=(r['before']|mask if enabled else r['before']&~mask):
            raise ValueError('canonical formation setter differs')
        if r['caller']!={2:0x89cb0d,4:0x89cb15,8:0x89cb1d}[mask]:
            raise ValueError('setter came from a different producer')
    if len(requests)!=expected*(3 if config.get('alt') else 2):
        raise ValueError('point canonical requests missing')
    for values in requests.values():
        if [r['mask']for r in values]!=[2,4,8] or values[0]['before']!=0 or any(a['after']!=b['before'] for a,b in zip(values,values[1:])):
            raise ValueError('canonical option sequence differs')
    if [r['rank']for r in rows if r['event']=='formation-authored-rank']!=[0,1,2,3,0,1]:
        raise ValueError('public mixed-rank creation missing')
    result=first_tick(rows)
    return {'first_tick':result,'signature':signature(result),'policy_requests':len(requests),
            'selected_event_counts':{k:counts[k]for k in EVENTS},'queued':bool(config.get('shift'))}


def header(results):
    out='/* Original1.27 public input and first physical owner tick. Generated by verify_wc3_formation_policy_trace.py. */\n'
    out+='static uint32_t const formation_policy_inputs[4][7]={\n'
    out+=''.join('    {'+','.join('0x%08xu'%v for v in r['first_tick']['input'])+'},\n'for r in results[:4])+'};\n'
    out+='static uint32_t const formation_policy_geometry[4][4]={\n'
    out+=''.join('    {'+','.join('0x%08xu'%v for v in r['first_tick']['geometry'])+'},\n'for r in results[:4])+'};\n'
    out+='static uint32_t const formation_policy_ticks[4][6][19]={\n'
    for r in results[:4]:
        out+='  {\n'+''.join('    {'+','.join('0x%08xu'%v for v in row)+'},\n'for row in r['first_tick']['tick'])+'  },\n'
    return out+'};\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures',type=Path,nargs=4)
    parser.add_argument('--header',type=Path)
    args=parser.parse_args()
    results=[verify([json.loads(l)for l in p.read_text().splitlines()])for p in args.captures]
    if results[0]['signature']!=results[2]['signature'] or results[1]['signature']!=results[3]['signature']:
        raise ValueError('first complete tick does not repeat')
    if args.header:args.header.write_text(header(results))
    print(json.dumps({'repeat_first_tick':True,'cases':len(results),'scalar_words':4*6*19}))

if __name__=='__main__':main()
