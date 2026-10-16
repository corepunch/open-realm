"""Versioned ordinary-move inputs and exact, pointer-normalized expectations.

Words are unsigned raw binary32/integer words, never rounded decimal samples.
Missing observations in historical reports are explicit None, never empty state.
"""
import hashlib
import json
from pathlib import Path

VERSION = 1
GAME_SHA = 'd51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236'
CRT_SHA = '86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f'
DEFAULT_MANIFEST = Path(__file__).parent/'fixtures/retail-owner-baseline-1.27.json'


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def case_output(case):
    fields = ('initial_state','normalized_states','initial_route','trajectory','arrival_tick',
              'initial_admission','arrivals','next_order_admission','complete_dispatch',
              'reclaimed_payloads_by_class','user_order_reclaimed')
    return json.loads(json.dumps({field:case[field] for field in fields}))


def first_difference(actual, expected, path='$'):
    if type(actual) is not type(expected):
        return path+': type differs'
    if isinstance(actual,dict):
        if actual.keys()!=expected.keys():return path+': fields differ'
        pairs=((key,actual[key],value) for key,value in expected.items())
    elif isinstance(actual,list):
        if len(actual)!=len(expected):return path+': length differs'
        pairs=((index,a,e) for index,(a,e) in enumerate(zip(actual,expected)))
    else:
        return None if actual==expected else path+f': {actual!r} != {expected!r}'
    for key,a,e in pairs:
        error=first_difference(a,e,path+'.'+str(key))
        if error:return error
    return None


def load_manifest(path=DEFAULT_MANIFEST):
    data=json.loads(Path(path).read_text())
    if data['version']!=VERSION:raise ValueError('unsupported baseline manifest version')
    if data['build']!={'game_sha256':GAME_SHA,'crt_sha256':CRT_SHA}:
        raise ValueError('baseline build hashes differ')
    if canonical_digest({k:data['map'][k] for k in ('bounds_bits','terrain_words','support_height_bits')})!=data['map']['sha256']:
        raise ValueError('terrain data hash differs')
    if data['clock']['configured_interval_bits']!=0x3cf5c290:
        raise ValueError('original decimal interval differs')
    if data['clock']['advance_bits']!=0x3d000000:
        raise ValueError('baseline adapter requires controlled 1/32 advancement')
    if data['map']['bounds_bits']!=[0,0,0x44000000,0x44000000]:
        raise ValueError('baseline adapter requires 512-square no-file map')
    entity=data['entities'][0]
    if entity['rawcode']!='hfoo' or entity['unit_identity']!=[126,900] or entity['ability_identity']!=[124,902]:
        raise ValueError('unsupported preexisting unit/ability identity')
    if data['clock']['initial_bits']!=0 or data['clock']['epoch']!=0:
        raise ValueError('baseline requires initially idle clock origin')
    raw=(Path(path).parent/data['expectations']['file']).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=data['expectations']['sha256']:
        raise ValueError('baseline expectations hash differs')
    expected=json.loads(raw)
    if expected['version']!=VERSION:raise ValueError('unsupported snapshot version')
    if len(data['scenarios'])!=len(expected['cases']):raise ValueError('baseline case count differs')
    for scenario,case in zip(data['scenarios'],expected['cases']):
        if scenario['id']!=case['id']:raise ValueError('baseline case identity differs')
        commands=scenario['commands']
        if not 1<=len(commands)<=2 or commands[0]['entry']!='680320':
            raise ValueError('unsupported baseline initial command')
        if len(commands)==2 and commands[1]['entry']!='693490':
            raise ValueError('unsupported FIFO command')
        if scenario['termination']!='queues_empty_pools_reclaimed_visual_idle':
            raise ValueError('unsupported termination')
    return data,expected


def verify_case(case, expected):
    actual=case_output(case)
    error=first_difference(actual,expected['output'])
    if error:raise ValueError('baseline '+expected['id']+' '+error)
    return canonical_digest(actual)


def historical_motion_tick(case, tick=0):
    """Normalize recorded fields only; this historical report lacks event/clock state."""
    from verify_wc3_pathing_numeric import bits
    step=case['steps'][tick]
    return dict(phase='elapsed',owner_tick=[step['tick'],None],clock=None,
        motion=dict(time=None,epoch=None,position=step['position_bits'],velocity=step['velocity_bits'],maximum=None,facing=None),
        grids=[dict(lane=0,level=n,cells=cells) for n,cells in enumerate(case['hierarchy_lane0_states'])],
        paths=[dict(owner='mover',identity=case['retained_member_path_identity'],
                    fine=[bits(x) for x in case['fine_route']],indices=[step['waypoint'],case['accelerated_indices'][tick][1],None,None],adaptive=None,flags=None)],
        budgets=dict(search_work=case['scheduler_work']),group=None,events=None,dispatch=None)
