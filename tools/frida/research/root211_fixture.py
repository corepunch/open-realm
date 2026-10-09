"""Keep the complete Root211 original timeline; erase process pointers only."""
from collections import Counter
import copy

IGNORED={'metadata','installed','loading-key','trace-end','preload-file'}
COUNTS={'marker':1068,'commit-begin':498,'commit-end':498,'visual-begin':938,
        'visual-end':938,'root-producer':3,'prepend-facing':3,'facing-task':3,
        'turn-request':3,'cohort':4,'owner-begin':474,'owner-end':474}


def normalize(rows):
    result=[]
    pointers={}
    def visit(value):
        if isinstance(value,list):return [visit(v)for v in value]
        if not isinstance(value,dict):return value
        out={}
        for k,v in value.items():
            if k in ('group','mover','ability') and isinstance(v,str):
                out[k]=pointers.setdefault((k,v),sum(p[0]==k for p in pointers)+1)
            else:out[k]=visit(v)
        return out
    for row in rows:
        if row['event'] not in IGNORED:result.append(visit(copy.deepcopy(row)))
    validate(result)
    return result


def validate(rows):
    if dict(Counter(r['event']for r in rows))!=COUNTS:raise ValueError('incomplete Root timeline')
    def events(name):return [r for r in rows if r['event']==name]
    if [r['tick']for r in events('root-producer')]!=[50]*3:raise ValueError('missing public Root producer')
    if any(r['heading']!=0x408ba057 or r['caller']!='0x4270b1'for r in events('prepend-facing')):
        raise ValueError('RootAngle capture differs')
    if [r['tick']for r in events('facing-task')]!=[50,50,174]:raise ValueError('approach/facing order differs')
    if any(r['heading']!=0x408ba057 for r in events('facing-task')):raise ValueError('d0176 heading differs')
    turns=events('turn-request')
    if [r['tick']for r in turns]!=[50,50,174] or any(
        (r['angle'],r['turn'],r['receiver'],r['caller'])!=(0x408ba057,0x3dcccccd,0xd0196,'0x60032c')for r in turns):
        raise ValueError('internal turn bridge contract differs')
    cohorts=events('cohort')
    if [r['requestFlags']for r in cohorts]!=[512,512,0,512]:raise ValueError('physical request policy differs')
    if [r['parameter']for r in cohorts]!=[0x3dcccccd,0x3dcccccd,0x7f7fffff,0x3dcccccd]:
        raise ValueError('physical turn parameter differs')
    angular=[r for r in events('owner-begin')if r['flags']&512]
    if len(angular)!=57 or any(len(r['members'])!=1 for r in angular):raise ValueError('angular owner visits differ')
    if any(r['parameter']!=0x3dcccccd for r in angular):raise ValueError('angular owner scalar differs')
    markers=[r['value']for r in events('marker')]
    required=['R211 tick=53 label=unit1 x=1024.000 y=512.000 f=237.684 o=0',
              'R211 tick=182 label=unit2 x=1536.012 y=1008.634 f=165.931 o=852165',
              'R211 tick=183 label=unit2 x=1536.000 y=1024.000 f=187.450 o=852165',
              'R211 tick=260 label=complete',
              'R211 tick=260 label=unit0 x=512.000 y=512.000 f=256.867 o=0',
              'R211 tick=260 label=unit2 x=1536.000 y=1024.000 f=239.015 o=0']
    if any(v not in markers for v in required):raise ValueError('Stop/relocation/settled heading differs')
    return 57


def header(rows):
    validate(rows)
    turns=[r for r in rows if r['event']=='turn-request']
    cohorts=[r for r in rows if r['event']=='cohort']
    values=[[t['angle'],t['turn'],*c['requestPoint'],c['requestFlags']]for t,c in zip(turns[:2],cohorts[:2])]
    return ('/* Original Root211b same-position public Root requests; generated from retail only. */\n'
            'static uint32_t const retail_root211_requests[][5]={\n'+
            ''.join('    {'+','.join('0x%08xu'%v for v in row)+'},\n'for row in values)+'};\n')
