#!/usr/bin/env python3
"""Validate ordinary mixed-rank formation adjustment and retained-route stages."""
import argparse
from collections import Counter
import json
import struct
from pathlib import Path
from verify_wc3_formation_policy_trace import EVENTS as POLICY_EVENTS, first_tick, verify as verify_policy

SOURCE_HASHES = {
 'wc3_formation_blocked_probe.j':'254367b1e097ee1dea61b69ff37c2d34e855eeb5c10ff555badac524edc275c5',
 'trace_wc3_pathfinding.py':'311efb4f250dcaf68eeac00542f90d119a3b9c6ed86c38636f93a7b9dc49e7d4',
 'wc3_pathfinding.js':'6c0321aeeae03816633fcb1a61604d65134af473e7c7215ebb887f70c0c52ff6',
 'make_wc3_pathfinding_map.py':'27c37491910eb3d03573c63ff2a6dad28db3d61f8af72caf6042950f74d4a0d8',
 'wc3_ui_input.c':'7a457bb0cd14c8b633a5f1c06ccf8b7e0a125e7503daf1323bfce2b9c548cfb4',
 'wc3_ui_input.exe':'675c94df1da19308c32bbcfb3f21ea1d6c35abe71d6515b1876d725b60e12f83',
 'wc3_ui_input.exe.so':'0b66ec6a9c3206dcaa427221b432173e6d45247dfeb4d1ff235155db75bd8bd8',
 'map':'34419ab58ac311889d812e80003dba337085f1a4ba1b5d16a726f37375c6f681',
}
EVENTS=(*POLICY_EVENTS,'formation-member-destination','formation-group-route')
HELD=[0x140000,0x200000,0x200000,0x200000,0x100000,0x200000]
RELEASED=[0x140000,*([0x100000]*5)]



def heading_delta(target, current):
    value=lambda word:struct.unpack('<f',struct.pack('<I',word))[0]
    bits=lambda number:struct.unpack('<I',struct.pack('<f',number))[0]
    reverse=value(bits(value(current)-value(target)))
    if reverse<0 and -reverse>value(0x40490fdb):return bits(-value(0x40c90fdb)-reverse)
    if reverse>value(0x40490fdb):return bits(value(0x40c90fdb)-reverse)
    return bits(reverse)^0x80000000


def verify(rows):
    policy=verify_policy(rows,SOURCE_HASHES,'formation_blocked',lambda r:first_tick(r,HELD))
    if policy['queued'] or rows[0]['pointInput']['alt']:
        raise ValueError('blocked witness requires ordinary unqueued input')
    footer=rows[-1]['counts'];counts=Counter(r['event']for r in rows)
    for event in ('formation-member-destination','formation-group-route'):
        if counts[event]!=footer.get(event):raise ValueError('truncated '+event)
    adjusted=[r for r in rows if r['event']=='formation-member-destination']
    routes=[r for r in rows if r['event']=='formation-group-route']
    if len(adjusted)!=6 or len(routes)<2:raise ValueError('lost fresh/cached boundary')
    first=routes[0]
    if first['before']['coarseCount'] or first['before']['coarseIndex']!=0xffffffff or first['after']['coarseCount']!=2 or first['after']['coarseIndex']!=0:
        raise ValueError('fresh coarse route differs')
    if any(r['before']!=first['after'] or r['after']!=first['after'] or r['result']!=1 or r['identity']!=first['identity'] or r['afterFormation']!=first['afterFormation'] for r in routes[1:]):
        raise ValueError('cached route changed or rebuilt')
    phase=None;begin=None;decide=None;velocities=[];decisions=[];adjustments=[];ticks=[]
    for row in rows:
        event=row['event']
        if event=='pair-group-phase-begin':
            if phase is not None:raise ValueError('nested member phase')
            phase=row['phase'];begin=row
            if phase=='decide':decisions=[];adjustments=[]
            elif phase=='commit':velocities=[]
            else:raise ValueError('unknown member phase')
        elif event=='formation-member-destination':
            if phase!='decide' or row['identity']!=begin['identity']:raise ValueError('adjustment outside decision')
            adjustments.append(row)
        elif event=='motion-decision' and phase=='decide':decisions.append(row)
        elif event=='velocity-commit' and phase=='commit':velocities.append(row)
        elif event=='pair-group-phase-end':
            if phase!=row['phase'] or row['identity']!=begin['identity']:raise ValueError('unpaired phase')
            if phase=='decide':
                decide=row
                movers=[m['mover']for m in row['members']]
                observed=[d['mover']for d in decisions]
                if observed!=[m for m in movers if m in observed]:raise ValueError('member decision order differs')
                if bool(adjustments)!=bool(begin['flags']&0x10000):raise ValueError('refresh adjustment boundary differs')
                if adjustments and len(adjustments)!=len(row['members']):raise ValueError('lost member adjustment')
                for m in row['members']:
                    d=next((d for d in decisions if d['mover']==m['mover']),None)
                    if d is None:
                        if not m['row'][10]&0x10000 or m['row'][8]:raise ValueError('lost member decision')
                    elif m['row'][8:10]!=[min(d['nextSpeed'],m['pose'][6]),d['nextHeading']]:raise ValueError('decision output differs')
            else:
                if decide is None or row['identity']!=decide['identity'] or len(velocities)!=len(row['members']):raise ValueError('lost ordered commit')
                share=not begin['flags']&8 and not any(m['row'][10]&0x210000 or m['moverFlags']&0x01000000 for m in begin['members'])
                cap=min(m['pose'][6]for m in begin['members']) if share else 0x7f7fffff
                words=[]
                for a,b,c,v in zip(decide['members'],begin['members'],row['members'],velocities):
                    if not a['mover']==b['mover']==c['mover']==v['mover'] or a['row']!=b['row'] or a['row']!=c['row'] or v['before']!=b['pose'] or v['after']!=c['pose'] or v['heading']!=c['row'][9] or v['requested'][0]!=v['speed']:raise ValueError('decision did not reach commit')
                    if v['speed']!=min(c['row'][8],cap) or v['requested'][1]!=heading_delta(v['heading'],v['before'][7]):raise ValueError('speed cap or stored heading delta differs')
                    words.append([*c['row'][3:5],*c['row'][6:11],*v['after'],*v['requested'],v['speed'],v['heading']])
                ticks.append({'clock':velocities[0]['clock'],'words':words,'flags':begin['flags']})
                decide=None
            phase=None
    if phase is not None or decide is not None or len(ticks)!=len(routes):raise ValueError('incomplete owner ticks')
    if [w[6]for w in ticks[0]['words']]!=HELD or [w[6]for w in ticks[1]['words']]!=RELEASED:
        raise ValueError('held/released transition differs')
    if ticks[0]['words'][0][2:4]!=[0x41c80000,0x41a80000] or adjusted[0]['after'][6:8]!=ticks[0]['words'][0][2:4]:
        raise ValueError('blocked fallback differs')
    if any(a['member'][3:5]!=b[0:2] or a['member'][10]!=(HELD[i]&0x200000) or a['after'][10]!=(0x40000 if i==0 else HELD[i]&0x200000) for i,(a,b)in enumerate(zip(adjusted,ticks[0]['words']))):
        raise ValueError('classification must precede adjustment')
    return {'first_tick':policy['first_tick'],'signature':policy['signature'],
            'replay':ticks[:2],'owner_ticks':len(ticks),'commits':sum(len(t['words'])for t in ticks),
            'selected_event_counts':{k:counts[k]for k in EVENTS}}


def header(results):
    def words(row):return '{'+','.join('0x%08xu'%v for v in row)+'}'
    out='/* Native formation110 fresh and first cached ticks; generated by verify_wc3_formation_blocked_trace.py. */\n'
    out+='static uint32_t const formation_blocked_inputs[2][7]={\n'+''.join('    '+words(r['first_tick']['input'])+',\n'for r in results)+'};\n'
    out+='static uint32_t const formation_blocked_clocks[2][2][3]={\n'+''.join('    {'+','.join(words(t['clock'])for t in r['replay'])+'},\n'for r in results)+'};\n'
    out+='static uint32_t const formation_blocked_ticks[2][2][6][19]={\n'
    for r in results:
        out+='  {\n'
        for t in r['replay']:out+='    {\n'+''.join('      '+words(w)+',\n'for w in t['words'])+'    },\n'
        out+='  },\n'
    return out+'};\n'


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('captures',nargs=2,type=Path);parser.add_argument('--header',type=Path)
    args=parser.parse_args();results=[verify([json.loads(l)for l in p.read_text().splitlines()])for p in args.captures]
    if results[0]['signature']!=results[1]['signature']:raise ValueError('first tick does not repeat')
    if args.header:args.header.write_text(header(results))
    print(json.dumps(results,indent=2))
