#!/usr/bin/env python3
"""Extract Town ranking facts and outputs from the read-only original calls.

The range, mobility and enabled-weapon facts are observed dependency returns.
Engine fixtures isolate ranking; they do not replay the public trajectory.
"""

def rows_from_events(r):
    rows=[]
    for scene in range(38):
     events=[e for e in r if e.get('scene')==scene]
     start=next(i for i,e in enumerate(events) if e['event']=='rank')
     finish=next(i for i in range(start+1,len(events)) if events[i]['event']=='rank-end')
     q=events[start:finish+1];target=q[0]['candidate'];key=q[-1]['key']
     def pred(addr):
      return [e['result'] for e in q if e['event']=='predicate' and e['address']==addr]
     def scalars(kind):return [e['word'] for e in q if e['event']=='priority-scalar' and e['kind']==kind]
     ranges=[e for e in q if e['event']=='range' and e['caller']=='49da13'];assert len(ranges)==1
     multiplier=[e for e in q if e['event']=='damage-multiplier'];assert len(multiplier)==1
     assert not any(e['present'] for e in q if e['event']=='priority-ability')
     assert target['ec']==0 and target['state194']==0
     life=scalars('life');mana=scalars('mana');speed=scalars('speed')
     mobile=bool(target['move']!='0x0' and (pred('5fb040')[0] or any(pred('48e260'))))
     assert len(speed)==(2 if mobile else 0)
     rows.append(dict(scene=scene,rawcode=target['rawcode'],defense=target['e4'],worker=bool(target['class248']&0x10),
       mobile=mobile,armed=bool(pred('495b90') and pred('495b90')[0]),in_range=bool(ranges[0]['accepted'] or any(pred('68c250'))),
       summoned=bool(target['class248']&4),illusion=bool(target['flags']&0x40000000),
       life=life[0] if life else 0x43d20000,mana=mana[0] if mana else 0,
       source_speed=speed[0] if speed else 0x43870000,target_speed=speed[1] if speed else 0,
       multiplier=multiplier[0]['word'],key=key))
    return rows

def header(rows):
    s='/* Original Group338 hard-map first ranking calls. Range/mobility/weapon facts\n * are observed helper returns; these fixtures isolate ranking from movement. */\nstatic struct {\n uint32_t rawcode,defense; bool worker,mobile,armed,in_range,summoned,illusion;\n uint32_t life,mana,source_speed,target_speed,multiplier,low,high;\n} const group338_town_rows[]={\n'
    for x in rows:
     values=[x[k] for k in ['rawcode','defense','worker','mobile','armed','in_range','summoned','illusion','life','mana','source_speed','target_speed','multiplier']]+x['key']
     s+=' {'+','.join(('true' if v else 'false') if isinstance(v,bool) else '0x%08xu'%v for v in values)+'},\n'
    s+='};\n'
    return s
