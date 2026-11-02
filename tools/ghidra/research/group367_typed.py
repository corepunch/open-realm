"""Resolve only witnessed Unit pointers; preserve literal slot/generation words."""
import copy
from verify_wc3_pathing_group359 import projection as base_projection


def canonical(unit):
    if not isinstance(unit,dict):raise ValueError('missing associated Unit')
    pair=[unit.get('slot'),unit.get('generation')]
    if any(not isinstance(x,int)or x<0 or x>0xffffffff for x in pair)or pair==[0xffffffff,0xffffffff]:raise ValueError('invalid canonical Unit identity')
    return pair


def projection(rows):
    rows=copy.deepcopy(rows)
    sources={}
    for row in rows:
        if row.get('event')=='sleep-pause-source':
            source=row['candidate'];pair=canonical(source)
            if source['rawcode']!=0x55647265 or source['owner']!=0:raise ValueError('unexpected public caster')
            address=int(row['address'],16)
            if not address:raise ValueError('null caster witness')
            sources[address]=pair
            row['address']={'canonical':pair}
        if row.get('event')!='suspension-push':continue
        params=row['parameters'];association=row.get('association')
        if len(params)!=1:raise ValueError('wrong suspension argument count')
        address=params[0]
        if not address:
            if association is not None:raise ValueError('association on null argument')
            continue
        if not isinstance(association,dict)or int(association['address'],16)!=address:raise ValueError('argument/address witness differs')
        unit=association['snapshot'];pair=canonical(unit)
        if sources.get(address)!=pair:raise ValueError('argument is not the witnessed live Sleep caster')
        row['parameters']=[{'canonical':pair}]
        row['association']={'canonical':pair}
        row['candidate']=unit
    return base_projection(rows)
